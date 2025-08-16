// Logger.h - Simplified logging system
#pragma once

#include <Arduino.h>

class Logger {
private:
    bool sdInitialized = true;
    
    // Helper functions
    void sanitizeForCSV(const char* input, char* output, size_t maxLen);
    bool writeToFile(uint32_t from, uint32_t to, const char* senderName, uint32_t channelHash, const char* message, const char* filename);
    
public:
    Logger();
    
    // Initialize SD card once at startup
    bool initializeSD();
    
    // Simple logging functions - write immediately
    bool addMessageLog(uint32_t from, uint32_t to, const char* senderName, uint32_t channelHash, const char* message);
    bool addCommandLog(uint32_t from, uint32_t to, const char* senderName, uint32_t channelHash, const char* command);
    bool addTelemetryLog(int16_t current_ma, int16_t voltage_mv);
    
    // Status check
    bool isInitialized() { return sdInitialized; }
};

extern Logger meshLogger;