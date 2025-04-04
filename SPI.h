#pragma once
#include <stdint.h>
class SPISettings {
public:
    SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) {}
};
class SPIClass {
public:
    void begin() {}
    void beginTransaction(SPISettings settings) {}
    uint8_t transfer(uint8_t data) { return 0; }
    void endTransaction() {}
};
extern SPIClass SPI;