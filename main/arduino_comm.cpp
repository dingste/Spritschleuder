#include "arduino_comm.h"
#include "config.h"
#include "esp_log.h"
#include "recipes.h"
#include "flow_calibration.h"

static const char *TAG = "arduino_comm";

ArduinoComm& ArduinoComm::getInstance() {
    static ArduinoComm instance;
    return instance;
}

bool ArduinoComm::init() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = UART_BAUD_RATE;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_DEFAULT;
    
    uart_driver_install(UART_PORT_NUM, 2048, 0, 0, nullptr, 0);
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Ask the Arduino to run its LED sequence. It acknowledges only after the
    // four individual outputs and the simultaneous run have completed.
    uart_flush_input(UART_PORT_NUM);
    const uint8_t test_command[5] = {0xAA, 0xFE, 0, 0, 0x55};
    uint8_t ack[3] = {};
    uart_write_bytes(UART_PORT_NUM, test_command, sizeof(test_command));
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(50));
    const int ack_bytes = uart_read_bytes(UART_PORT_NUM, ack, sizeof(ack), pdMS_TO_TICKS(7000));
    const bool test_acknowledged = ack_bytes == sizeof(ack) &&
        ack[0] == 0xBB && ack[1] == 0xFE && ack[2] == 0x55;
    if (test_acknowledged) {
        ESP_LOGI(TAG, "Arduino-Pumpentest abgeschlossen (ACK empfangen)");
    } else {
        ESP_LOGW(TAG, "Kein ACK nach Arduino-Pumpentest empfangen (Bytes: %d)", ack_bytes);
    }

    const bool calibration_acknowledged = test_acknowledged &&
        setCalibration(FlowCalibration::millisecondsPerMl());
    if (calibration_acknowledged) {
        ESP_LOGI(TAG, "Globaler Durchflussfaktor vom Arduino bestaetigt (%u ms/ml)",
                 FlowCalibration::millisecondsPerMl());
    } else if (test_acknowledged) {
        ESP_LOGE(TAG, "Arduino hat den globalen Durchflussfaktor nicht bestaetigt");
    }
    
    xTaskCreate(uart_task, "uart_task", 4096, this, 5, &uart_task_handle);
    ESP_LOGI(TAG, "UART Kommunikation initialisiert");
    return test_acknowledged && calibration_acknowledged;
}

bool ArduinoComm::setCalibration(uint16_t milliseconds_per_ml) {
    if (milliseconds_per_ml < FlowCalibration::MIN_MILLISECONDS_PER_ML ||
        milliseconds_per_ml > FlowCalibration::MAX_MILLISECONDS_PER_ML) return false;

    const uint8_t frame[5] = {
        0xAA, 0xFD, static_cast<uint8_t>(milliseconds_per_ml >> 8),
        static_cast<uint8_t>(milliseconds_per_ml & 0xFF), 0x55
    };
    uint8_t ack[3] = {};
    std::lock_guard<std::mutex> lock(uart_mutex);
    uart_flush_input(UART_PORT_NUM);
    if (uart_write_bytes(UART_PORT_NUM, frame, sizeof(frame)) != sizeof(frame)) return false;
    if (uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(100)) != ESP_OK) return false;
    const int count = uart_read_bytes(UART_PORT_NUM, ack, sizeof(ack), pdMS_TO_TICKS(500));
    return count == sizeof(ack) && ack[0] == 0xBB && ack[1] == 0xFD && ack[2] == 0x55;
}

void ArduinoComm::sendOrder(const CocktailOrder& order) {
    std::lock_guard<std::mutex> lock(queue_mutex);
    const Recipe &recipe = order.recipe;
    uint32_t total = 0;
    for (uint8_t i = 0; i < recipe.num_ingredients; ++i) total += recipe.ingredients[i].amount_ml;
    for (uint8_t i = 0; i < recipe.num_ingredients; ++i) {
        uint32_t ml = recipe.ingredients[i].amount_ml * order.quantity_ml / total;
        command_queue.push(ArduinoCommand{recipe.ingredients[i].pump_id, static_cast<uint16_t>(ml)});
    }
}

void ArduinoComm::abort() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        ++abort_generation;
        std::queue<ArduinoCommand> empty; command_queue.swap(empty);
    }
    // Dauer 0 ist ein sofortiges AUS und funktioniert fuer jede Firmware-Version.
    std::lock_guard<std::mutex> lock(uart_mutex);
    for (uint8_t pump = 0; pump < 4; ++pump) {
        const uint8_t frame[5] = {0xAA, pump, 0, 0, 0x55};
        uart_write_bytes(UART_PORT_NUM, frame, sizeof(frame));
    }
    uart_wait_tx_done(UART_PORT_NUM, pdMS_TO_TICKS(50));
    ESP_LOGW(TAG, "NOT-AUS: alle Pumpen gestoppt");
}

void ArduinoComm::uart_task(void *param) {
    auto *comm = static_cast<ArduinoComm*>(param);
    
    while (true) {
        ArduinoCommand cmd;
        uint32_t generation;
        
        {
            std::lock_guard<std::mutex> lock(comm->queue_mutex);
            if (comm->command_queue.empty()) {
                vTaskDelay(10 / portTICK_PERIOD_MS);
                continue;
            }
            cmd = comm->command_queue.front();
            comm->command_queue.pop();
            generation = comm->abort_generation.load();
        }
        
        // Protokoll: [START][PUMP_ID][AMOUNT_ML_HI][AMOUNT_ML_LO][END]
        uint8_t buffer[5] = {
            0xAA,                    // Start byte
            cmd.pump_id,             // Pumpen ID
            (uint8_t)(cmd.amount_ml >> 8),  // Menge High byte
            (uint8_t)(cmd.amount_ml & 0xFF), // Menge Low byte
            0x55                     // End byte
        };
        
        {
            std::lock_guard<std::mutex> lock(comm->uart_mutex);
            // Falls NOT-AUS zwischen Entnahme und UART-Zugriff lag, darf dieser
            // bereits entnommene Befehl die Pumpe keinesfalls erneut starten.
            if (generation != comm->abort_generation.load()) continue;
            uart_write_bytes(UART_PORT_NUM, buffer, sizeof(buffer));
        }
        ESP_LOGI(TAG, "Befehl gesendet: Pumpe %d, %d ml", cmd.pump_id, cmd.amount_ml);
        
        // Alle Startbefehle eines Rezeptes direkt hintereinander senden. Der Arduino
        // schaltet die Pumpen unabhaengig und damit gleichzeitig.
        taskYIELD();
    }
}
