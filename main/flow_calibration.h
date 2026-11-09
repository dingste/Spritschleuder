#pragma once

#include <cstdint>
#include "esp_err.h"

namespace FlowCalibration {

constexpr uint16_t DEFAULT_MILLISECONDS_PER_ML = 450;
constexpr uint16_t MIN_MILLISECONDS_PER_ML = 50;
constexpr uint16_t MAX_MILLISECONDS_PER_ML = 60000;

esp_err_t init();
uint16_t millisecondsPerMl();
esp_err_t setMillisecondsPerMl(uint16_t value);

}  // namespace FlowCalibration
