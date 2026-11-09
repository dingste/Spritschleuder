// Arduino firmware for the pump controller.
#include <Arduino.h>
#include "pump_controller.h"

constexpr uint8_t PUMP_COUNT = PumpController::PUMP_COUNT;

void setup() {
    Serial.begin(115200);
    PumpController::begin();
}

void loop() {
    while (Serial.available() >= 5) {
        // Protocol: 0xAA [PUMP_ID] [AMOUNT_ML_HI] [AMOUNT_ML_LO] 0x55
        if (Serial.read() == 0xAA) {
            const uint8_t pumpId = Serial.read();
            const uint8_t durationHi = Serial.read();
            const uint8_t durationLo = Serial.read();
            const uint8_t endByte = Serial.read();

            if (endByte == 0x55 && pumpId == PumpController::SELF_TEST_COMMAND_ID &&
                durationHi == 0 && durationLo == 0) {
                PumpController::runSelfTest();
                // ACK only after the complete five-second LED sequence.
                Serial.write(0xBB);
                Serial.write(pumpId);
                Serial.write(0x55);
            } else if (endByte == 0x55 && pumpId < PUMP_COUNT) {
                const uint16_t amountMl =
                    (static_cast<uint16_t>(durationHi) << 8) | durationLo;
                PumpController::setForAmountMl(pumpId, amountMl);

                Serial.write(0xBB);
                Serial.write(pumpId);
                Serial.write(0x55);
            }
        }
    }

    PumpController::update();
}
