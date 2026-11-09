#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "config.h"
#include "cocktail_server.h"
#include "arduino_comm.h"
#include "display_manager.h"
#include "wifi_manager.h"
#include "button_manager.h"
#include "recipes.h"

static const char *TAG = "cocktailbot_main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "CocktailBot wird gestartet...");
    
    // NVS initialisieren
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(RecipeStore::getInstance().init());
    
    // Display initialisieren
    DisplayManager::getInstance().init();
    DisplayManager::getInstance().showBootScreen();
    
    ESP_ERROR_CHECK(WifiManager::getInstance().init());
    
    // Arduino Kommunikation starten
    const bool arduino_test_passed = ArduinoComm::getInstance().init();
    ButtonManager::getInstance().init();
    
    // HTTP/WebSocket Server starten
    CocktailServer::getInstance().start();
    if (!WifiManager::getInstance().isConfigured()) {
        DisplayManager::getInstance().showInfo();
        ESP_LOGI(TAG, "Setup-Ansicht bereit; WLAN-QR-Code wird angezeigt");
    } else if (arduino_test_passed) {
        DisplayManager::getInstance().showReady();
        ESP_LOGI(TAG, "CocktailBot bereit; Arduino-Pumpentest bestaetigt");
    } else {
        DisplayManager::getInstance().showError("ARDUINO UART FEHLER");
        ESP_LOGE(TAG, "Webserver bereit, Arduino-Pumpentest nicht bestaetigt");
    }
}
