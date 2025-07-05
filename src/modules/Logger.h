// Logger.h - Header file for buffered logging system
#pragma once

#include <Arduino.h>
#include <vector>
#include <string>

struct MessageLogEntry {
    uint32_t timestamp;
    uint32_t from;
    uint32_t to;
    char senderName[64];
    uint8_t channelIndex;
    char message[256];
};

struct TelemetryLogEntry {
    uint32_t timestamp;
    int16_t current_ma;
    int16_t voltage_mv;
    // Add other telemetry fields as needed
};

class BufferedLogger {
private:
    static const size_t MESSAGE_BUFFER_SIZE = 20;  // Adjust based on your memory constraints
    static const size_t TELEMETRY_BUFFER_SIZE = 50;  // Adjust based on your memory constraints

    // Flush thresholds as percentage of buffer capacity (0-100)
    static const uint8_t MESSAGE_FLUSH_THRESHOLD = 80;  // 80% full
    static const uint8_t TELEMETRY_FLUSH_THRESHOLD = 80;  // 80% full

    std::vector<MessageLogEntry> messageBuffer;
    std::vector<TelemetryLogEntry> telemetryBuffer;

    bool flushToSDCard();

    // Immediate write functions (no buffering)
    bool writeMessageToSDCard(uint32_t from, uint32_t to, const char* senderName, uint8_t channelIndex, const char* message);
    bool writeTelemetryToSDCard(int16_t current_ma, int16_t voltage_mv);

public:
    BufferedLogger();

    // Mode control - set these to control behavior
    bool immediateMessageWrite = true;  // Set to false for buffering
    bool immediateTelemetryWrite = false;  // Keep telemetry buffered by default

    bool addMessageLog(uint32_t from, uint32_t to, const char* senderName, uint8_t channelIndex, const char* message);
    bool addTelemetryLog(int16_t current_ma, int16_t voltage_mv);

    // Force an immediate write to SD card
    bool forceFlush();

    // Get current buffer levels
    size_t getMessageBufferSize() { return messageBuffer.size(); }
    size_t getTelemetryBufferSize() { return telemetryBuffer.size(); }
    size_t getMessageBufferCapacity() { return MESSAGE_BUFFER_SIZE; }
    size_t getTelemetryBufferCapacity() { return TELEMETRY_BUFFER_SIZE; }

    // Mode control functions
    void setImmediateMessageWrite(bool immediate) { immediateMessageWrite = immediate; }
    void setImmediateTelemetryWrite(bool immediate) { immediateTelemetryWrite = immediate; }
};

extern BufferedLogger bufferedLogger;