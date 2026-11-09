#include "flow_calibration.h"

#include <atomic>
#include "nvs.h"

namespace FlowCalibration {
namespace {

std::atomic<uint16_t> ms_per_ml{DEFAULT_MILLISECONDS_PER_ML};

}  // namespace

esp_err_t init() {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("flow", NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;

    uint16_t stored = DEFAULT_MILLISECONDS_PER_ML;
    err = nvs_get_u16(nvs, "ms_per_ml", &stored);
    nvs_close(nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if (err != ESP_OK) return err;
    if (stored < MIN_MILLISECONDS_PER_ML || stored > MAX_MILLISECONDS_PER_ML) {
        return ESP_OK;
    }
    ms_per_ml.store(stored);
    return ESP_OK;
}

uint16_t millisecondsPerMl() {
    return ms_per_ml.load();
}

esp_err_t setMillisecondsPerMl(uint16_t value) {
    if (value < MIN_MILLISECONDS_PER_ML || value > MAX_MILLISECONDS_PER_ML) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("flow", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(nvs, "ms_per_ml", value);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    if (err == ESP_OK) ms_per_ml.store(value);
    return err;
}

}  // namespace FlowCalibration
