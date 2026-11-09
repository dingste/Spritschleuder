#pragma once

#include <string>
#include <mutex>
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "recipes.h"

class DisplayManager {
public:
    static DisplayManager& getInstance();
    
    void init();
    void showBootScreen();
    void showMainMenu();
    void showMixing(const Recipe& recipe, int quantity_ml, uint16_t milliseconds_per_ml);
    void showReady();
    void showAborted();
    void showInfo();
    void toggleInfo();
    void showError(const std::string& error);
    
private:
    DisplayManager() = default;
    static void displayTask(void *);
    void render();
    void sendCommand(uint8_t command, const uint8_t *data = nullptr, size_t length = 0);
    void fill(uint16_t color);
    void rectangle(int x, int y, int width, int height, uint16_t color);
    void text(int x, int y, const char *value, uint16_t color, int scale = 1);
    void character(int x, int y, char value, uint16_t color, int scale);
    void flush();
    bool initialized = false;
    spi_device_handle_t spi = nullptr;
    uint16_t *framebuffer = nullptr;
    TaskHandle_t task_handle = nullptr;
    std::mutex state_mutex;
    enum class State { Boot, Ready, Mixing, Info, Aborted, Error } state = State::Boot;
    std::string detail;
    int quantity_ml = 0;
    int64_t mixing_started_us = 0;
    uint32_t mixing_duration_ms = 0;
    int64_t state_until_us = 0;
};
