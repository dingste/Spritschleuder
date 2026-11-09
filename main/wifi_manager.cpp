#include "wifi_manager.h"
#include "config.h"
#include "display_manager.h"
#include <cstring>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "lwip/inet.h"

static const char *TAG = "wifi";
WifiManager &WifiManager::getInstance() { static WifiManager i; return i; }

bool WifiManager::loadCredentials() {
    nvs_handle_t nvs;
    if (nvs_open("wifi", NVS_READONLY, &nvs) != ESP_OK) return false;
    char s[33] = {}, p[65] = {}; size_t sl = sizeof(s), pl = sizeof(p);
    bool ok = nvs_get_str(nvs, "ssid", s, &sl) == ESP_OK &&
              nvs_get_str(nvs, "password", p, &pl) == ESP_OK && sl > 1;
    nvs_close(nvs); if (ok) { ssid = s; password = p; } return ok;
}

esp_err_t WifiManager::init() {
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap(); esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, eventHandler, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, eventHandler, this));
    configured = loadCredentials(); if (configured) startStation(); else startAccessPoint(); return ESP_OK;
}

void WifiManager::startAccessPoint() {
    wifi_config_t ap = {};
    std::strncpy(reinterpret_cast<char *>(ap.ap.ssid), AP_SSID, sizeof(ap.ap.ssid));
    std::strncpy(reinterpret_cast<char *>(ap.ap.password), AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.ssid_len = std::strlen(AP_SSID); ap.ap.channel = 1; ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP)); ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start()); ESP_LOGI(TAG, "Setup-AP '%s', http://192.168.4.1", AP_SSID);
}

void WifiManager::startStation() {
    wifi_config_t sta = {};
    std::strncpy(reinterpret_cast<char *>(sta.sta.ssid), ssid.c_str(), sizeof(sta.sta.ssid));
    std::strncpy(reinterpret_cast<char *>(sta.sta.password), password.c_str(), sizeof(sta.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start()); ESP_LOGI(TAG, "Verbinde mit '%s'", ssid.c_str());
}

void WifiManager::eventHandler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    auto *self = static_cast<WifiManager *>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        self->connected = false;
        if (!self->configured.load()) return;
        if (self->retry_count++ < WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
        } else {
            self->resetToSetupMode();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto *e = static_cast<ip_event_got_ip_t *>(data); self->connected = true; self->retry_count = 0;
        ESP_LOGI(TAG, "Bereit unter http://" IPSTR, IP2STR(&e->ip_info.ip));
    }
}

void WifiManager::resetToSetupMode() {
    const std::string failed_ssid = ssid;
    ESP_LOGE(TAG, "WLAN '%s' nach %d Versuchen nicht erreichbar; setze Konfiguration zurück",
             failed_ssid.c_str(), WIFI_MAXIMUM_RETRY + 1);

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WLAN-Station konnte nicht gestoppt werden: %s", esp_err_to_name(err));
        return;
    }

    err = clearCredentials();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WLAN-Zugangsdaten konnten nicht gelöscht werden: %s", esp_err_to_name(err));
    }

    ssid.clear();
    password.clear();
    connected = false;
    configured = false;
    retry_count = 0;

    startAccessPoint();
    DisplayManager::getInstance().showInfo();
}

std::string WifiManager::ipAddress() const {
    esp_netif_ip_info_t info = {}; auto *n = esp_netif_get_handle_from_ifkey(configured.load() ? "WIFI_STA_DEF" : "WIFI_AP_DEF");
    if (!n || esp_netif_get_ip_info(n, &info) != ESP_OK) return "0.0.0.0";
    char out[16]; esp_ip4addr_ntoa(&info.ip, out, sizeof(out)); return out;
}

esp_err_t WifiManager::saveCredentials(const std::string &s, const std::string &p) {
    if (s.empty() || s.size() > 32 || p.size() > 64) return ESP_ERR_INVALID_ARG;
    nvs_handle_t n; ESP_RETURN_ON_ERROR(nvs_open("wifi", NVS_READWRITE, &n), TAG, "nvs_open");
    esp_err_t e = nvs_set_str(n, "ssid", s.c_str()); if (e == ESP_OK) e = nvs_set_str(n, "password", p.c_str());
    if (e == ESP_OK) e = nvs_commit(n);
    nvs_close(n);
    return e;
}

esp_err_t WifiManager::clearCredentials() {
    nvs_handle_t n; ESP_RETURN_ON_ERROR(nvs_open("wifi", NVS_READWRITE, &n), TAG, "nvs_open");
    esp_err_t e = nvs_erase_all(n);
    if (e == ESP_OK) e = nvs_commit(n);
    nvs_close(n);
    return e;
}
