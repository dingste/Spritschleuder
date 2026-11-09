#include "cocktail_server.h"
#include "arduino_comm.h"
#include "config.h"
#include "display_manager.h"
#include "recipes.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdlib>
#include <cstring>
#include <string>

static const char *TAG = "web";
static const char PAGE[] = R"HTML(<!doctype html><html lang="de"><meta name="viewport" content="width=device-width"><meta charset="utf-8"><link rel="icon" href="data:,"><title>Spritschleuder</title><style>body{font:16px system-ui;max-width:680px;margin:auto;padding:20px;background:#171717;color:#eee}section{background:#292929;padding:18px;margin:14px 0;border-radius:12px}button,input,select{font:inherit;padding:10px;margin:5px;border-radius:7px;border:0}button{background:#f57c00;color:white;cursor:pointer}.ok{color:#ff9800}.stop{background:#f57c00;font-weight:bold}.pumps{display:grid;grid-template-columns:auto 1fr;align-items:center}</style><h1>🍹 Spritschleuder</h1><section id="setup"><h2>WLAN einrichten</h2><input id="ssid" placeholder="WLAN-Name"><input id="pass" type="password" placeholder="Passwort"><button onclick="wifi()">Speichern & Neustarten</button></section><section><h2>Cocktail mixen</h2><select id="drink"></select><select id="ml"><option>40</option><option selected>200</option><option>400</option></select> ml <button onclick="mix()">Mixen</button><button class="stop" onclick="abortMix()">SOFORT STOPP</button><p id="msg"></p></section><section><h2>Neue Mixtur</h2><input id="recipeName" maxlength="24" placeholder="Name"><div class="pumps"><label>Pumpe 1</label><input id="p0" type="number" min="0" max="1000" value="0"><label>Pumpe 2</label><input id="p1" type="number" min="0" max="1000" value="0"><label>Pumpe 3</label><input id="p2" type="number" min="0" max="1000" value="0"><label>Pumpe 4</label><input id="p3" type="number" min="0" max="1000" value="0"></div><small>Anteile in ml; 0 lässt eine Pumpe aus.</small><br><button onclick="addRecipe()">Mixtur speichern</button></section><section><button onclick="resetWifi()">WLAN-Konfiguration löschen</button></section><script>async function post(u,o){let r=await fetch(u,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}),t=await r.text();if(!r.ok)throw Error(t);return t}async function loadRecipes(){try{let response=await fetch('/api/recipes');if(!response.ok)throw Error('Mixturen konnten nicht geladen werden');let rs=await response.json();let select=document.getElementById('drink');select.innerHTML='';rs.forEach((r,i)=>select.add(new Option(r.name,i)));if(!rs.length)document.getElementById('msg').textContent='Keine Mixturen vorhanden'}catch(e){document.getElementById('msg').textContent='Fehler beim Laden der Mixturen: '+e.message}}async function wifi(){let message=document.getElementById('msg');try{await post('/api/wifi',{ssid:document.getElementById('ssid').value,password:document.getElementById('pass').value});message.textContent='Gespeichert. Gerät startet neu…'}catch(e){message.textContent=e.message}}async function mix(){let select=document.getElementById('drink'),message=document.getElementById('msg');if(!select.value){message.textContent='Bitte zuerst eine Mixtur auswählen';return}message.textContent='Bestellung wird gesendet…';try{await post('/api/order',{cocktail_id:Number(select.value),quantity_ml:Number(document.getElementById('ml').value)});message.className='ok';message.textContent='Bestellung angenommen'}catch(e){message.className='';message.textContent='Bestellung fehlgeschlagen: '+e.message}}async function abortMix(){let message=document.getElementById('msg');try{await post('/api/abort',{});message.className='';message.textContent='Mischung abgebrochen'}catch(e){message.textContent='Stopp fehlgeschlagen: '+e.message}}async function addRecipe(){let value=id=>document.getElementById(id).value,message=document.getElementById('msg');try{await post('/api/recipes',{name:value('recipeName'),pump0_ml:+value('p0'),pump1_ml:+value('p1'),pump2_ml:+value('p2'),pump3_ml:+value('p3')});document.getElementById('recipeName').value='';['p0','p1','p2','p3'].forEach(id=>document.getElementById(id).value=0);await loadRecipes();message.className='ok';message.textContent='Mixtur gespeichert'}catch(e){message.textContent='Speichern fehlgeschlagen: '+e.message}}async function resetWifi(){if(confirm('WLAN wirklich löschen?'))await post('/api/wifi/reset',{})}loadRecipes()</script></html>)HTML";

CocktailServer &CocktailServer::getInstance() { static CocktailServer i; return i; }
static void jsonReply(httpd_req_t *r, const char *body, const char *status = nullptr) { if (status) httpd_resp_set_status(r, status); httpd_resp_set_type(r, "application/json"); httpd_resp_sendstr(r, body); }
static bool readBody(httpd_req_t *r, std::string &body) {
    if (r->content_len <= 0 || r->content_len > 512) return false;
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

esp_err_t CocktailServer::start() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG(); cfg.server_port = HTTP_PORT; cfg.max_uri_handlers = 10;
    ESP_RETURN_ON_ERROR(httpd_start(&server, &cfg), TAG, "HTTP server");
    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, pageHandler, nullptr}, {"/api/status", HTTP_GET, statusHandler, nullptr},
        {"/api/order", HTTP_POST, orderHandler, nullptr}, {"/api/wifi", HTTP_POST, wifiHandler, nullptr},
        {"/api/wifi/reset", HTTP_POST, resetWifiHandler, nullptr},
        {"/api/recipes", HTTP_GET, recipesHandler, nullptr}, {"/api/recipes", HTTP_POST, addRecipeHandler, nullptr},
        {"/api/abort", HTTP_POST, abortHandler, nullptr},
    };
    for (const auto &route : routes) ESP_ERROR_CHECK(httpd_register_uri_handler(server, &route));
    ESP_LOGI(TAG, "Webserver gestartet"); return ESP_OK;
}
void CocktailServer::stop() { if (server) { httpd_stop(server); server = nullptr; } }
esp_err_t CocktailServer::pageHandler(httpd_req_t *r) { httpd_resp_set_type(r, "text/html; charset=utf-8"); return httpd_resp_send(r, PAGE, HTTPD_RESP_USE_STRLEN); }
esp_err_t CocktailServer::statusHandler(httpd_req_t *r) {
    auto &w = WifiManager::getInstance();
    std::string out = std::string("{\"configured\":") + (w.isConfigured() ? "true" : "false") +
        ",\"connected\":" + (w.isConnected() ? "true" : "false") + ",\"ip\":\"" + w.ipAddress() + "\"}";
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
    ArduinoComm::getInstance().sendOrder(o); DisplayManager::getInstance().showMixing(o.recipe, o.quantity_ml);
}

static std::string jsonEscape(const std::string &value) {
    std::string out; for (char c : value) { if (c == '"' || c == '\\') out += '\\'; if (static_cast<unsigned char>(c) >= 0x20) out += c; } return out;
}
esp_err_t CocktailServer::recipesHandler(httpd_req_t *r) {
    auto recipes = RecipeStore::getInstance().all(); std::string out = "[";
    for (size_t i = 0; i < recipes.size(); ++i) { if (i) out += ','; out += "{\"name\":\"" + jsonEscape(recipes[i].name) + "\"}"; }
    out += ']'; jsonReply(r, out.c_str()); return ESP_OK;
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
    ArduinoComm::getInstance().abort(); DisplayManager::getInstance().showAborted(); jsonReply(r, "{\"status\":\"aborted\"}"); return ESP_OK;
}
