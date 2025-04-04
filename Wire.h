#pragma once
#include <stdint.h>
class TwoWire {
public:
    void begin() {}
    void beginTransmission(uint8_t address) {}
    uint8_t write(uint8_t data) { return 1; }
    uint8_t endTransmission() { return 0; }
    uint8_t requestFrom(uint8_t address, uint8_t quantity) { return quantity; }
    uint8_t read() { return 0xFF; }
};
extern TwoWire Wire;