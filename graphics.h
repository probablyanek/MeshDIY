#pragma once
#include <Arduino.h>

class Graphics {
public:
    static void initialize();
    static void displayStatus(const char* text, uint8_t x, uint8_t y);
    static void displayMessage(const String& msg, float rssi);
    static void showNetworkTopology();
};