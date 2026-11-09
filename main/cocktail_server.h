#pragma once
#include <esp_http_server.h>
#include <string>
#include "recipes.h"

struct CocktailOrder { Recipe recipe; int quantity_ml; };

class CocktailServer {
public:
    static CocktailServer &getInstance();
    static void markAborted();
    esp_err_t start();
    void stop();
private:
    CocktailServer() = default;
    static esp_err_t pageHandler(httpd_req_t *);
    static esp_err_t orderHandler(httpd_req_t *);
    static esp_err_t wifiHandler(httpd_req_t *);
    static esp_err_t resetWifiHandler(httpd_req_t *);
    static esp_err_t statusHandler(httpd_req_t *);
    static esp_err_t recipesHandler(httpd_req_t *);
    static esp_err_t addRecipeHandler(httpd_req_t *);
    static esp_err_t abortHandler(httpd_req_t *);
    static esp_err_t calibrationHandler(httpd_req_t *);
    void processOrder(const CocktailOrder &);
    httpd_handle_t server = nullptr;
};
