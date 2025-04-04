#pragma once
#include <stdint.h>

#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)

namespace ScanI2C {
    enum class I2CPort { WIRE };
    struct Address {
        I2CPort port;
        uint8_t address;
    };
}