#include "button_manager.h"
#include "arduino_comm.h"
#include "config.h"
#include "display_manager.h"
#include "cocktail_server.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "buttons";
ButtonManager &ButtonManager::getInstance() { static ButtonManager manager; return manager; }
void ButtonManager::init() {
    gpio_config_t config = {}; config.pin_bit_mask = (1ULL << ABORT_BUTTON_PIN) | (1ULL << INFO_BUTTON_PIN);
    config.mode = GPIO_MODE_INPUT; config.pull_up_en = GPIO_PULLUP_ENABLE; config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&config)); xTaskCreate(task, "buttons", 2048, this, 6, &task_handle);
}
void ButtonManager::task(void *) {
    int last_abort = 1, last_info = 1;
    while (true) {
        int abort_level = gpio_get_level(static_cast<gpio_num_t>(ABORT_BUTTON_PIN));
        int info_level = gpio_get_level(static_cast<gpio_num_t>(INFO_BUTTON_PIN));
        if (!abort_level && last_abort) {
            ArduinoComm::getInstance().abort(); CocktailServer::markAborted(); DisplayManager::getInstance().showAborted();
            ESP_LOGW(TAG, "Mischung per Geraetetaste abgebrochen");
        }
        if (!info_level && last_info) DisplayManager::getInstance().toggleInfo();
        last_abort = abort_level; last_info = info_level; vTaskDelay(pdMS_TO_TICKS(30));
    }
}
