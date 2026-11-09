#include "cocktail_server.h"
#include "arduino_comm.h"
#include "config.h"
#include "display_manager.h"
#include "recipes.h"
#include "wifi_manager.h"
#include "flow_calibration.h"
#include "local_discovery.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

static const char *TAG = "web";
static std::mutex order_state_mutex;
static bool order_active = false;
static bool order_aborted = false;
static std::string active_recipe;
static int64_t order_started_us = 0;
static uint32_t order_duration_ms = 0;
static std::string jsonEscape(const std::string &value);

namespace {
constexpr char WEB_BASE_PATH[] = "/web";

}

CocktailServer &CocktailServer::getInstance() { static CocktailServer i; return i; }
static void jsonReply(httpd_req_t *r, const char *body, const char *status = nullptr) { if (status) httpd_resp_set_status(r, status); httpd_resp_set_type(r, "application/json"); httpd_resp_sendstr(r, body); }
static bool readBody(httpd_req_t *r, std::string &body) {
    if (r->content_len <= 0 || r->content_len > 4096) return false;
    body.resize(r->content_len); int got = 0;
    while (got < r->content_len) { int n = httpd_req_recv(r, body.data() + got, r->content_len - got); if (n <= 0) return false; got += n; }
    return true;
}
static bool jsonString(const std::string &body, const char *key, std::string &value) {
    std::string token = std::string("\"") + key + "\""; size_t p = body.find(token);
    if (p == std::string::npos || (p = body.find(':', p + token.size())) == std::string::npos || (p = body.find('"', p + 1)) == std::string::npos) return false;
    size_t end = body.find('"', p + 1); if (end == std::string::npos) return false; value = body.substr(p + 1, end - p - 1); return true;
}
static bool jsonInt(const std::string &body, const char *key, int &value) {
    std::string token = std::string("\"") + key + "\""; size_t p = body.find(token);
    if (p == std::string::npos || (p = body.find(':', p + token.size())) == std::string::npos) return false;
    char *end = nullptr; long number = std::strtol(body.c_str() + p + 1, &end, 10); if (end == body.c_str() + p + 1) return false; value = static_cast<int>(number); return true;
}
static bool jsonNumber(const std::string &body, const char *key, double &value) {
    std::string token = std::string("\"") + key + "\""; size_t p = body.find(token);
    if (p == std::string::npos || (p = body.find(':', p + token.size())) == std::string::npos) return false;
    char *end = nullptr; value = std::strtod(body.c_str() + p + 1, &end);
    return end != body.c_str() + p + 1;
}
static bool parseCsv(const std::string &input, std::vector<std::vector<std::string>> &rows) {
    std::vector<std::string> row;
    std::string field;
    bool in_quotes = false, after_quote = false;
    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < input.size() && input[i + 1] == '"') { field += '"'; ++i; }
                else { in_quotes = false; after_quote = true; }
            } else field += c;
        } else if (after_quote) {
            if (c == ',') { row.push_back(field); field.clear(); after_quote = false; }
            else if (c == '\n' || c == '\r') {
                if (c == '\r' && i + 1 < input.size() && input[i + 1] == '\n') ++i;
                row.push_back(field); field.clear(); rows.push_back(row); row.clear(); after_quote = false;
            } else if (c != ' ' && c != '\t') return false;
        } else if (c == '"') {
            if (!field.empty()) return false;
            in_quotes = true;
        } else if (c == ',') { row.push_back(field); field.clear(); }
        else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < input.size() && input[i + 1] == '\n') ++i;
            row.push_back(field); field.clear();
            bool nonempty = false; for (const auto &cell : row) nonempty |= !cell.empty();
            if (nonempty) rows.push_back(row);
            row.clear();
        } else field += c;
    }
    if (in_quotes) return false;
    if (!field.empty() || !row.empty() || after_quote) {
        row.push_back(field);
        bool nonempty = false; for (const auto &cell : row) nonempty |= !cell.empty();
        if (nonempty) rows.push_back(row);
    }
    return !rows.empty();
}
static bool parseCsvAmount(const std::string &field, int &amount) {
    if (field.empty()) { amount = 0; return true; }
    char *end = nullptr;
    const long value = std::strtol(field.c_str(), &end, 10);
    if (end == field.c_str() || *end != '\0' || value < 0 || value > 1000) return false;
    amount = static_cast<int>(value);
    return true;
}
static std::string csvEscape(const std::string &value) {
    std::string escaped = "\"";
    for (char c : value) { if (c == '"') escaped += '"'; escaped += c; }
    escaped += '"';
    return escaped;
}

esp_err_t CocktailServer::start() {
    esp_vfs_spiffs_conf_t storage = {};
    storage.base_path = WEB_BASE_PATH;
    storage.partition_label = "webdata";
    storage.max_files = 4;
    storage.format_if_mount_failed = false;
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&storage), TAG, "SPIFFS web assets");

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = HTTP_PORT;
    cfg.max_uri_handlers = 12;
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 4;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "HTTP server");
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, pageHandler, nullptr}, {"/api/status", HTTP_GET, statusHandler, nullptr},
        {"/api/order", HTTP_POST, orderHandler, nullptr}, {"/api/wifi", HTTP_POST, wifiHandler, nullptr},
        {"/api/wifi/reset", HTTP_POST, resetWifiHandler, nullptr},
        {"/api/recipes", HTTP_GET, recipesHandler, nullptr}, {"/api/recipes", HTTP_POST, addRecipeHandler, nullptr},
        {"/api/abort", HTTP_POST, abortHandler, nullptr},
        {"/api/calibration", HTTP_GET, calibrationHandler, nullptr},
        {"/api/calibration", HTTP_POST, calibrationHandler, nullptr},
        {"/api/recipes.csv", HTTP_GET, recipesCsvExportHandler, nullptr},
        {"/api/recipes/import", HTTP_POST, recipesCsvImportHandler, nullptr},
    };
    for (const auto &route : routes) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    if (LocalDiscovery::start() != ESP_OK)
        ESP_LOGW(TAG, "mDNS nicht verfuegbar; HTTP-Server laeuft, Geraetename eventuell nicht aufloesbar");
    ESP_LOGI(TAG, "HTTP-Server gestartet: %s", LocalDiscovery::URL); return ESP_OK;
}
void CocktailServer::stop() {
    if (server) {
        LocalDiscovery::stop();
        httpd_stop(server);
        server = nullptr;
    }
}
esp_err_t CocktailServer::pageHandler(httpd_req_t *r) {
    const std::string page_path = std::string(WEB_BASE_PATH) + "/index.html";
    FILE *file = std::fopen(page_path.c_str(), "rb");
    if (!file) return httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "Weboberflaeche fehlt");
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-cache");
    char buffer[1024];
    esp_err_t result = ESP_OK;
    size_t count;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0) {
        result = httpd_resp_send_chunk(r, buffer, count);
        if (result != ESP_OK) break;
    }
    if (std::ferror(file)) result = ESP_FAIL;
    std::fclose(file);
    if (result != ESP_OK) return result;
    return httpd_resp_send_chunk(r, nullptr, 0);
}
esp_err_t CocktailServer::statusHandler(httpd_req_t *r) {
    auto &w = WifiManager::getInstance();
    bool mixing;
    uint32_t progress;
    std::string recipe;
    {
        std::lock_guard<std::mutex> lock(order_state_mutex);
        const uint32_t elapsed_ms = static_cast<uint32_t>(std::max<int64_t>(0, (esp_timer_get_time() - order_started_us) / 1000));
        if (order_active && elapsed_ms >= order_duration_ms) order_active = false;
        mixing = order_active;
        progress = order_duration_ms ? std::min<uint64_t>(100, static_cast<uint64_t>(elapsed_ms) * 100 / order_duration_ms) : 0;
        if (order_aborted) progress = 0;
        recipe = active_recipe;
    }
    std::string out = std::string("{\"configured\":") + (w.isConfigured() ? "true" : "false") +
        ",\"connected\":" + (w.isConnected() ? "true" : "false") + ",\"ip\":\"" + w.ipAddress() +
        "\",\"mixing\":" + (mixing ? "true" : "false") + ",\"progress\":" + std::to_string(progress) +
        ",\"aborted\":" + (order_aborted ? "true" : "false") + ",\"recipe\":\"" + jsonEscape(recipe) + "\"}";
    jsonReply(r, out.c_str()); return ESP_OK;
}
esp_err_t CocktailServer::orderHandler(httpd_req_t *r) {
    std::string body; int id, ml;
    Recipe recipe;
    if (!readBody(r, body) || !jsonInt(body, "cocktail_id", id) || !jsonInt(body, "quantity_ml", ml) || id < 0 || !RecipeStore::getInstance().get(id, recipe) || ml < 20 || ml > 400) {
        jsonReply(r, "{\"error\":\"Cocktail oder Menge ungueltig\"}", "400 Bad Request"); return ESP_OK;
    }
    CocktailOrder o{recipe, ml};
    getInstance().processOrder(o); jsonReply(r, "{\"status\":\"ok\"}"); return ESP_OK;
}
esp_err_t CocktailServer::wifiHandler(httpd_req_t *r) {
    std::string body, ssid, password;
    if (!readBody(r, body) || !jsonString(body, "ssid", ssid) || !jsonString(body, "password", password) || WifiManager::getInstance().saveCredentials(ssid, password) != ESP_OK) {
        jsonReply(r, "{\"error\":\"WLAN-Daten ungueltig\"}", "400 Bad Request"); return ESP_OK;
    }
    jsonReply(r, "{\"status\":\"restarting\"}"); vTaskDelay(pdMS_TO_TICKS(300)); esp_restart(); return ESP_OK;
}
esp_err_t CocktailServer::resetWifiHandler(httpd_req_t *r) {
    esp_err_t e = WifiManager::getInstance().clearCredentials(); jsonReply(r, e == ESP_OK ? "{\"status\":\"restarting\"}" : "{\"error\":\"NVS\"}");
    if (e == ESP_OK) { vTaskDelay(pdMS_TO_TICKS(300)); esp_restart(); } return ESP_OK;
}
void CocktailServer::processOrder(const CocktailOrder &o) {
    ESP_LOGI(TAG, "Bestellung: %s (%d ml)", o.recipe.name.c_str(), o.quantity_ml);
    const uint16_t calibration = FlowCalibration::millisecondsPerMl();
    uint32_t total = 0, duration_ms = 0;
    for (uint8_t i = 0; i < o.recipe.num_ingredients; ++i) total += o.recipe.ingredients[i].amount_ml;
    if (total) for (uint8_t i = 0; i < o.recipe.num_ingredients; ++i) {
        const uint32_t amount_ml = o.recipe.ingredients[i].amount_ml * o.quantity_ml / total;
        duration_ms = std::max(duration_ms, amount_ml * calibration);
    }
    ArduinoComm::getInstance().sendOrder(o);
    DisplayManager::getInstance().showMixing(o.recipe, o.quantity_ml, calibration);
    {
        std::lock_guard<std::mutex> lock(order_state_mutex);
        active_recipe = o.recipe.name;
        order_started_us = esp_timer_get_time();
        order_duration_ms = std::max<uint32_t>(1, duration_ms);
        order_active = true;
        order_aborted = false;
    }
}

static std::string jsonEscape(const std::string &value) {
    std::string out; for (char c : value) { if (c == '"' || c == '\\') out += '\\'; if (static_cast<unsigned char>(c) >= 0x20) out += c; } return out;
}
esp_err_t CocktailServer::recipesHandler(httpd_req_t *r) {
    auto recipes = RecipeStore::getInstance().all(); std::string out = "[";
    for (size_t i = 0; i < recipes.size(); ++i) { if (i) out += ','; out += "{\"name\":\"" + jsonEscape(recipes[i].name) + "\"}"; }
    out += ']'; jsonReply(r, out.c_str()); return ESP_OK;
}
esp_err_t CocktailServer::recipesCsvExportHandler(httpd_req_t *r) {
    const auto recipes = RecipeStore::getInstance().all();
    std::string csv = "name,pump0_ml,pump1_ml,pump2_ml,pump3_ml\r\n";
    for (const auto &recipe : recipes) {
        uint16_t amounts[4] = {};
        for (uint8_t i = 0; i < recipe.num_ingredients; ++i) {
            const auto &ingredient = recipe.ingredients[i];
            if (ingredient.pump_id < 4) amounts[ingredient.pump_id] = ingredient.amount_ml;
        }
        csv += csvEscape(recipe.name);
        for (uint8_t pump = 0; pump < 4; ++pump) csv += "," + std::to_string(amounts[pump]);
        csv += "\r\n";
    }
    httpd_resp_set_type(r, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(r, "Content-Disposition", "attachment; filename=mixturen.csv");
    return httpd_resp_send(r, csv.c_str(), csv.size());
}
esp_err_t CocktailServer::recipesCsvImportHandler(httpd_req_t *r) {
    std::string body;
    std::vector<std::vector<std::string>> rows;
    if (!readBody(r, body)) {
        jsonReply(r, "{\"error\":\"CSV-Datei fehlt oder ist zu groß\"}", "400 Bad Request");
        return ESP_OK;
    }
    if (body.size() >= 3 && static_cast<uint8_t>(body[0]) == 0xEF &&
        static_cast<uint8_t>(body[1]) == 0xBB && static_cast<uint8_t>(body[2]) == 0xBF) body.erase(0, 3);
    if (!parseCsv(body, rows) || rows[0].size() != 5 || rows[0][0] != "name" ||
        rows[0][1] != "pump0_ml" || rows[0][2] != "pump1_ml" ||
        rows[0][3] != "pump2_ml" || rows[0][4] != "pump3_ml") {
        jsonReply(r, "{\"error\":\"CSV-Kopfzeile muss name,pump0_ml,pump1_ml,pump2_ml,pump3_ml lauten\"}", "400 Bad Request");
        return ESP_OK;
    }
    if (rows.size() < 2 || rows.size() - 1 > MAX_COCKTAILS) {
        jsonReply(r, "{\"error\":\"CSV muss zwischen 1 und 20 Mixturen enthalten\"}", "400 Bad Request");
        return ESP_OK;
    }

    std::vector<Recipe> imported;
    imported.reserve(rows.size() - 1);
    for (size_t row_index = 1; row_index < rows.size(); ++row_index) {
        const auto &row = rows[row_index];
        if (row.size() != 5 || row[0].empty() || row[0].size() > 24) {
            jsonReply(r, "{\"error\":\"Mixturname oder Spaltenzahl in CSV-Zeile ungueltig\"}", "400 Bad Request");
            return ESP_OK;
        }
        for (unsigned char c : row[0]) {
            if (c < 0x20 || c == 0x7f) {
                jsonReply(r, "{\"error\":\"Mixturnamen duerfen keine Steuerzeichen enthalten\"}", "400 Bad Request");
                return ESP_OK;
            }
        }
        Recipe recipe;
        recipe.name = row[0];
        for (uint8_t pump = 0; pump < 4; ++pump) {
            int amount = 0;
            if (!parseCsvAmount(row[pump + 1], amount)) {
                jsonReply(r, "{\"error\":\"Pumpenmenge in CSV muss eine Zahl von 0 bis 1000 sein\"}", "400 Bad Request");
                return ESP_OK;
            }
            if (amount) recipe.ingredients[recipe.num_ingredients++] = {pump, static_cast<uint16_t>(amount)};
        }
        if (!recipe.num_ingredients) {
            jsonReply(r, "{\"error\":\"Jede Mixtur muss mindestens eine Pumpe enthalten\"}", "400 Bad Request");
            return ESP_OK;
        }
        imported.push_back(recipe);
    }

    const esp_err_t result = RecipeStore::getInstance().replaceAll(imported);
    if (result != ESP_OK) {
        jsonReply(r, "{\"error\":\"Mixturen konnten nicht gespeichert werden\"}", "500 Internal Server Error");
        return ESP_OK;
    }
    const std::string response = "{\"status\":\"ok\",\"count\":" + std::to_string(imported.size()) + "}";
    jsonReply(r, response.c_str());
    return ESP_OK;
}
esp_err_t CocktailServer::addRecipeHandler(httpd_req_t *r) {
    std::string body, name; int amounts[4] = {};
    if (!readBody(r, body) || !jsonString(body, "name", name)) { jsonReply(r, "{\"error\":\"Name fehlt\"}", "400 Bad Request"); return ESP_OK; }
    Recipe recipe; recipe.name = name;
    for (int pump = 0; pump < 4; ++pump) {
        std::string key = "pump" + std::to_string(pump) + "_ml";
        if (!jsonInt(body, key.c_str(), amounts[pump]) || amounts[pump] < 0 || amounts[pump] > 1000) { jsonReply(r, "{\"error\":\"Pumpenmenge ungueltig\"}", "400 Bad Request"); return ESP_OK; }
        if (amounts[pump]) recipe.ingredients[recipe.num_ingredients++] = {static_cast<uint8_t>(pump), static_cast<uint16_t>(amounts[pump])};
    }
    esp_err_t result = RecipeStore::getInstance().add(recipe);
    if (result != ESP_OK) jsonReply(r, "{\"error\":\"Mixtur ungueltig oder Speicher voll\"}", "400 Bad Request"); else jsonReply(r, "{\"status\":\"ok\"}"); return ESP_OK;
}
esp_err_t CocktailServer::abortHandler(httpd_req_t *r) {
    ArduinoComm::getInstance().abort();
    markAborted();
    DisplayManager::getInstance().showAborted(); jsonReply(r, "{\"status\":\"aborted\"}"); return ESP_OK;
}
void CocktailServer::markAborted() {
    std::lock_guard<std::mutex> lock(order_state_mutex);
    order_active = false;
    order_aborted = true;
    order_duration_ms = 0;
}
esp_err_t CocktailServer::calibrationHandler(httpd_req_t *r) {
    if (r->method == HTTP_GET) {
        char response[64];
        const double seconds_per_2ml = FlowCalibration::millisecondsPerMl() / 500.0;
        std::snprintf(response, sizeof(response), "{\"seconds_per_2ml\":%.1f}", seconds_per_2ml);
        jsonReply(r, response);
        return ESP_OK;
    }

    std::string body;
    double seconds_per_2ml;
    if (!readBody(r, body) || !jsonNumber(body, "seconds_per_2ml", seconds_per_2ml) ||
        !std::isfinite(seconds_per_2ml) || seconds_per_2ml < 0.1 || seconds_per_2ml > 120.0) {
        jsonReply(r, "{\"error\":\"Laufzeit muss zwischen 0,1 und 120 Sekunden liegen\"}", "400 Bad Request");
        return ESP_OK;
    }

    const uint16_t old_factor = FlowCalibration::millisecondsPerMl();
    const uint16_t milliseconds_per_ml = static_cast<uint16_t>(std::lround(seconds_per_2ml * 500.0));
    esp_err_t err = FlowCalibration::setMillisecondsPerMl(milliseconds_per_ml);
    if (err != ESP_OK) {
        jsonReply(r, "{\"error\":\"Kalibrierung konnte nicht gespeichert werden\"}", "500 Internal Server Error");
        return ESP_OK;
    }
    if (!ArduinoComm::getInstance().setCalibration(milliseconds_per_ml)) {
        FlowCalibration::setMillisecondsPerMl(old_factor);
        jsonReply(r, "{\"error\":\"Arduino hat den Faktor nicht bestätigt\"}", "503 Service Unavailable");
        return ESP_OK;
    }
    jsonReply(r, "{\"status\":\"ok\"}");
    return ESP_OK;
}
