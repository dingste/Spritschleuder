#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "cocktail_server.h"

struct ArduinoCommand {
    uint8_t pump_id;
    uint16_t amount_ml;
};

class ArduinoComm {
public:
    static ArduinoComm& getInstance();
    
    bool init();
    bool setCalibration(uint16_t milliseconds_per_ml);
    void sendOrder(const CocktailOrder& order);
    void abort();
    
private:
    ArduinoComm() = default;
    
    static void uart_task(void *param);
    std::queue<ArduinoCommand> command_queue;
    std::mutex queue_mutex;
    std::mutex uart_mutex;
    std::atomic<uint32_t> abort_generation{0};
    TaskHandle_t uart_task_handle = nullptr;
};
