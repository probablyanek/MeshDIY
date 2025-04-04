#include <SPI.h>
#include <Wire.h>
#include "globals.h"
#include "power.h"
#include "mesh_service.h"
#include "routing.h"
#include "node_db.h"
#include "graphics.h"
#include "radiolib_wrapper.h"
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEUtils.h>

#define OLED_SDA   21
#define OLED_SCL   22
#define LORA_SCK   5
#define LORA_MISO  19
#define LORA_MOSI  27
#define LORA_CS    18
#define LORA_RST   14
#define LORA_IRQ   26
#define SX126X_BUSY 33
#define LED_PIN    2

#define SERVICE_UUID "00001234-5678-9ABC-DEF0-1234567890AB"
#define CHARACTERISTIC_UUID "0000CDEF-0123-4567-89AB-CDEF01234567"
NimBLECharacteristic* pCharacteristic;

// Global variables for frame processing
enum FrameState {
  WAITING_FOR_PREAMBLE,
  READING_HEADER,
  READING_PAYLOAD,
  READING_CRC,
  FRAME_COMPLETE
};

const uint8_t PREAMBLE_LENGTH = 8;
const uint8_t PREAMBLE_PATTERN[] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA}; // Example preamble
uint8_t buffer[512]; // Adjust size based on maximum expected frame
size_t bufferIndex = 0;
FrameState currentState = WAITING_FOR_PREAMBLE;
uint8_t headerLength = 0;
uint16_t payloadLength = 0;

// Structure to hold both message and signal info
struct ReceivedMessage {
    String content;
    float snr;
    float rssi;
};

class SimpleLoRa {
private:
    SX1278 radio = new Module(SS, DIO0, RESET, DIO1); // Adjust pins for your hardware
    float freq = 433.0; // 433 MHz
    float bw = 125.0;   // Bandwidth
    uint8_t sf = 9;     // Spreading factor
    uint8_t cr = 7;     // Coding rate
    uint8_t power = 17; // 17 dBm output
    uint16_t preamble = 8;

public:
    bool init() {
        int state = radio.begin(freq, bw, sf, cr, syncWord, power, preamble);
        
        if(state == RADIOLIB_ERR_NONE) {
            radio.setDio0Action(setFlag); // Set interrupt
            startReceive();
            return true;
        }
        return false;
    }

    void sendMessage(String message) {
        radio.transmit(message);
    }

    ReceivedMessage receiveMessage() {
        ReceivedMessage result;
        if(receiveFlag) {
            receiveFlag = false;
            
            String msg;
            int state = radio.readData(msg);
            
            if(state == RADIOLIB_ERR_NONE) {
                result.content = msg;
                result.snr = radio.getSNR();
                result.rssi = radio.getRSSI();
            }
            
            startReceive(); // Restart listening
        }
        return result;
    }

private:
    volatile bool receiveFlag = false;

    static void setFlag() {
        receiveFlag = true;
    }

    void startReceive() {
        radio.startReceive();
    }
};


const int I2C_OLED_ADDR = 0x3C;

#ifndef LOG_INFO
#define LOG_INFO(format, ...) Serial.printf("[INFO] " format "\n", ##__VA_ARGS__)
#endif

void consoleInit() {
    Serial.begin(115200);
    while (!Serial);
    delay(100);
}

void fsInit() {
    delay(50);
}

void initDeepSleep() {}
void PowerFSM_setup() {}

void setup() {

    NimBLEDevice::init("ESP32");
    NimBLEServer *pServer = NimBLEDevice::createServer();
    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
                      );
    pService->start();
    pServer->getAdvertising()->start();
  
    Serial.begin(9600);
    if(lora.init()) {
        Serial.println("LoRa initialized!");
    }

    consoleInit();
    fsInit();
    ScanI2C::Address screenAddr = {ScanI2C::I2CPort::WIRE, I2C_OLED_ADDR};

    SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

    initDeepSleep();
    PowerFSM_setup();

#ifdef LED_PIN
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
#endif


#ifdef LED_PIN
    while(true) {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        delay(500);
    }
#endif
}

void loop() {
    ReceivedMessage msg = lora.receiveMessage();
    if(!msg.content.isEmpty()) {
        // Validate and process received message
        if(msg.content.length() > 0 && msg.content.length() < 256) {
            // Update OLED display
            displayMessage(msg.content, msg.rssi);
            
            // Log message with timestamp and truncated content
            Serial.printf("[%lu] RX: %.32s... (RSSI: %.1f dBm)\n",
                         millis(),
                         msg.content.c_str(),
                         msg.rssi);
            
            // Simulate mesh forwarding with hop counter
            if(msg.content.startsWith("FORWARD:") && networkStats.packetsForwarded < MAX_HOPS) {
                String forwardMsg = "HOP-" + String(networkStats.packetsForwarded+1) + ":" + msg.content;
                lora.sendMessage(forwardMsg);
                networkStats.packetsForwarded++;
            }
        } else {
            // Handle invalid message length
            networkStats.crcErrors++;
            Serial.println("Invalid message length - potential buffer overflow");
            displayStatus("INVALID MSG LEN!", 0, 0);
        }
    }
    if (pCharacteristic->isWritten()) {
        uint8_t* newData = pCharacteristic->getData();
        size_t newDataLength = pCharacteristic->getLength();
        
        // Append new data to the buffer
        for (size_t i = 0; i < newDataLength; i++) {
          buffer[bufferIndex++] = newData[i];
          processIncomingByte(buffer[bufferIndex - 1]);
        }
        
        // Acknowledge receipt
        pCharacteristic->indicate();
      }
    }
    
void processIncomingByte(uint8_t byte) {
    switch (currentState) {
    case WAITING_FOR_PREAMBLE:
        // Check for preamble pattern
        static uint8_t preamblePos = 0;
        if (byte == PREAMBLE_PATTERN[preamblePos]) {
        preamblePos++;
        if (preamblePos >= PREAMBLE_LENGTH) {
            currentState = READING_HEADER;
            preamblePos = 0;
        }
        } else {
        preamblePos = 0;
        }
        break;
        
    case READING_HEADER:
        // First byte of header is header length (20-30 bytes)
        if (headerLength == 0) {
        headerLength = byte;
        if (headerLength < 20 || headerLength > 30) {
            resetState();
            return;
        }
        } else {
        // Collect header bytes
        if (bufferIndex - PREAMBLE_LENGTH == headerLength) {
            parseHeader();
            currentState = READING_PAYLOAD;
        }
        }
        break;
        
    case READING_PAYLOAD:
        // Check if payload is complete
        if (bufferIndex - PREAMBLE_LENGTH - headerLength == payloadLength) {
        currentState = READING_CRC;
        }
        break;
        
    case READING_CRC:
        // Check if CRC is complete
        if (bufferIndex - PREAMBLE_LENGTH - headerLength - payloadLength == 2) {
        verifyCRC();
        resetState();
        }
        break;
    }
}

void parseHeader() {
    // Example: Payload length is stored in bytes 2-3 of the header
    payloadLength = (buffer[PREAMBLE_LENGTH + 2] << 8) | buffer[PREAMBLE_LENGTH + 3];
    // Extract SF, BW, CR from header if needed for payload length calculation
}

void verifyCRC() {
    // Compute CRC-16/CCITT over preamble + header + payload
    uint16_t computedCRC = computeCRC(buffer, bufferIndex - 2); // Exclude CRC bytes
    uint16_t receivedCRC = (buffer[bufferIndex - 2] << 8) | buffer[bufferIndex - 1];
    
    if (computedCRC == receivedCRC) {
    decryptPayload(buffer + PREAMBLE_LENGTH + headerLength, payloadLength);
    } else {
    Serial.println("CRC mismatch, discarding frame");
    }
}

uint16_t computeCRC(uint8_t* data, size_t length) {
    // Implement CRC-16/CCITT algorithm
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
        if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
        } else {
        crc >>= 1;
        }
    }
    }
    return crc;
}

/**
 * @brief Mock decryption for prototype demonstration
 * @param encrypted Pointer to encrypted data
 * @param length Length of encrypted data
 * @note In real implementation, use AES-128 with proper key management
 */
void decryptPayload(uint8_t* encrypted, size_t length) {
    // XOR "decryption" for demonstration
    Serial.println("Decrypted payload:");
    for (size_t i = 0; i < length; i++) {
    Serial.printf("%02X ", encrypted[i]);
    }
    Serial.println();
}

/// Academic diagnostic functions
void printNetworkStatistics() {
    Serial.printf("\n--- Network Diagnostics ---\n");
    Serial.printf("Packets Received: %lu\n", networkStats.packetsReceived);
    Serial.printf("Packets Forwarded: %lu\n", networkStats.packetsForwarded);
    Serial.printf("CRC Errors: %lu\n", networkStats.crcErrors);
    Serial.printf("Avg RSSI: %.1f dBm\n", networkStats.avgRSSI);
    Serial.printf("Network ID: 0x%04X\n", NETWORK_ID);
}

void resetState() {
    currentState = WAITING_FOR_PREAMBLE;
    headerLength = 0;
    payloadLength = 0;
    bufferIndex = 0;
}

/// Display helper functions
namespace Screen {
    void initDisplay(const ScanI2C::Address& addr) {
        // Actual implementation in graphics/Screen.cpp
        return true;
    }
    
    void showSplashScreen(const char* text) {
        // Implementation in graphics/Screen.cpp
    }
    
    void updateSignalStrength(float rssi) {
        // Implementation in graphics/Screen.cpp
    }
    
    void addNetworkNode(uint8_t nodeId, float signalStrength) {
        // Implementation in graphics/Screen.cpp
    }
    
    void displayDiagnostics(uint32_t received, float rssi, uint32_t forwarded) {
        // Implementation in graphics/Screen.cpp
    }
}

/// Academic simulation helper
void simulateRoutingUpdate(const String& update) {
    // Parse theoretical routing table updates
    networkStats.packetsForwarded++;
    displayStatus("Routing Updated", 0, 20);
}
