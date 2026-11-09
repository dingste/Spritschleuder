#pragma once

#include <Arduino.h>

namespace PumpController {

constexpr uint8_t PUMP_COUNT = 4;
constexpr uint8_t CALIBRATION_COMMAND_ID = 0xFD;
constexpr uint8_t SELF_TEST_COMMAND_ID = 0xFE;

void begin();
void setForDuration(uint8_t pumpId, uint16_t durationMs);
void setForAmountMl(uint8_t pumpId, uint16_t amountMl);
void setMillisecondsPerMl(uint16_t value);
void update();
void runSelfTest();

}  // namespace PumpController
