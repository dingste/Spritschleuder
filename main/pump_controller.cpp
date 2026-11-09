#include "pump_controller.h"

namespace PumpController {
namespace {

// Arduino digital pins D2-D5; connect one LED with a series resistor per pin.
const uint8_t PUMP_PINS[PUMP_COUNT] = {2, 3, 4, 5};
// Calibration: milliseconds needed for one ml, one editable value per pump.
// 450 ms/ml retains the project's previous baseline until each pump is measured.
const uint16_t PUMP_MILLISECONDS_PER_ML[PUMP_COUNT] = {450, 450, 450, 450};
unsigned long pumpStops[PUMP_COUNT] = {};

void setAllLow() {
    for (uint8_t pump = 0; pump < PUMP_COUNT; ++pump) {
        digitalWrite(PUMP_PINS[pump], LOW);
        pumpStops[pump] = 0;
    }
}

}  // namespace

void begin() {
    for (uint8_t pump = 0; pump < PUMP_COUNT; ++pump) {
        pinMode(PUMP_PINS[pump], OUTPUT);
        digitalWrite(PUMP_PINS[pump], LOW);
    }
}

void setForDuration(uint8_t pumpId, uint16_t durationMs) {
    if (pumpId >= PUMP_COUNT) return;

    if (durationMs == 0) {
        digitalWrite(PUMP_PINS[pumpId], LOW);
        pumpStops[pumpId] = 0;
        return;
    }

    digitalWrite(PUMP_PINS[pumpId], HIGH);
    pumpStops[pumpId] = millis() + durationMs;
}

void setForAmountMl(uint8_t pumpId, uint16_t amountMl) {
    if (pumpId >= PUMP_COUNT) return;
    if (amountMl == 0) {
        setForDuration(pumpId, 0);
        return;
    }

    const uint32_t durationMs =
        static_cast<uint32_t>(amountMl) * PUMP_MILLISECONDS_PER_ML[pumpId];
    digitalWrite(PUMP_PINS[pumpId], HIGH);
    pumpStops[pumpId] = millis() + durationMs;
}

void update() {
    const unsigned long now = millis();
    for (uint8_t pump = 0; pump < PUMP_COUNT; ++pump) {
        if (pumpStops[pump] && static_cast<long>(now - pumpStops[pump]) >= 0) {
            digitalWrite(PUMP_PINS[pump], LOW);
            pumpStops[pump] = 0;
        }
    }
}

void runSelfTest() {
    setAllLow();
    for (uint8_t pump = 0; pump < PUMP_COUNT; ++pump) {
        digitalWrite(PUMP_PINS[pump], HIGH);
        delay(1000);
        digitalWrite(PUMP_PINS[pump], LOW);
    }

    for (uint8_t pump = 0; pump < PUMP_COUNT; ++pump) {
        digitalWrite(PUMP_PINS[pump], HIGH);
    }
    delay(1000);
    setAllLow();
}

}  // namespace PumpController
