// Logger.cpp - Simplified logging system
#include "Logger.h"
#include "configuration.h"
#include "SimpleTimeManager.h"
#include <SD.h>

Logger meshLogger;

Logger::Logger() {
    sdInitialized = true;
}

bool Logger::initializeSD() {
    if (sdInitialized) {
        return true; // Already initialized
    }
    
    LOG_DEBUG("Initializing SD card...");
    
    if (!SD.begin()) {
        LOG_ERROR("SD card initialization failed");
        return false;
    }
    
    // Test basic functionality
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        LOG_ERROR("No SD card detected");
        return false;
    }
    
    LOG_INFO("SD card initialized successfully (Card Type: %d)", cardType);
    sdInitialized = true;
    return true;
}

bool Logger::addMessageLog(uint32_t from, uint32_t to, const char* senderName, uint32_t channelHash, const char* message) {
    if (!sdInitialized) {
        LOG_WARN("SD card not initialized, cannot log message");
        return false;
    }
    
    return writeToFile(from, to, senderName, channelHash, message, "/message_log.csv");
}

bool Logger::addCommandLog(uint32_t from, uint32_t to, const char* senderName, uint32_t channelHash, const char* command) {
    if (!sdInitialized) {
        LOG_WARN("SD card not initialized, cannot log command");
        return false;
    }
    
    return writeToFile(from, to, senderName, channelHash, command, "/command_log.csv");
}

bool Logger::addTelemetryLog(int16_t current_ma, int16_t voltage_mv) {
    if (!sdInitialized) {
        LOG_WARN("SD card not initialized, cannot log telemetry");
        return false;
    }
    
    char csvLine[128];
    char timestamp[32];
    
    String timeStr = getTimeManager()->getTimeString();
    strncpy(timestamp, timeStr.c_str(), sizeof(timestamp) - 1);
    timestamp[sizeof(timestamp) - 1] = '\0';
    
    int lineBytes = snprintf(csvLine, sizeof(csvLine), "%s,%d,%d\n", timestamp, current_ma, voltage_mv);
    
    if (lineBytes <= 0 || lineBytes >= sizeof(csvLine)) {
        LOG_ERROR("Telemetry CSV formatting failed");
        return false;
    }
    
    LOG_DEBUG("Writing telemetry to SD card");
    
    File telemetryFile = SD.open("/telemetry_log.csv", FILE_APPEND);
    if (!telemetryFile) {
        LOG_ERROR("Failed to open telemetry log file");
        return false;
    }
    
    // Add header if file is empty
    if (telemetryFile.size() == 0) {
        size_t headerWritten = telemetryFile.println("timestamp,current_ma,voltage_mv");
        if (headerWritten == 0) {
            LOG_ERROR("Failed to write telemetry header");
            telemetryFile.close();
            return false;
        }
    }
    
    // Write data
    size_t written = telemetryFile.write((const uint8_t*)csvLine, lineBytes);
    telemetryFile.flush();
    telemetryFile.close();
    
    bool success = (written == lineBytes);
    if (success) {
        LOG_DEBUG("Telemetry logged successfully");
    } else {
        LOG_ERROR("Telemetry write failed: %d/%d bytes", written, lineBytes);
    }
    
    return success;
}

void Logger::sanitizeForCSV(const char* input, char* output, size_t maxLen) {
    if (!input) {
        strncpy(output, "NULL", maxLen - 1);
        output[maxLen - 1] = '\0';
        return;
    }

    size_t inPos = 0, outPos = 0;

    while (input[inPos] && outPos < maxLen - 1) {
        char c = input[inPos];

        // Handle problematic characters
        if (c == '"') {
            // Escape quotes for CSV
            if (outPos < maxLen - 2) {
                output[outPos++] = '"';
                output[outPos++] = '"';
            }
        } else if (c == '\n' || c == '\r') {
            // Replace newlines with space
            output[outPos++] = ' ';
        } else if (c >= 32 && c <= 126) {
            // Keep printable ASCII
            output[outPos++] = c;
        } else {
            // Replace weird characters with placeholder
            output[outPos++] = '?';
        }
        inPos++;
    }
    output[outPos] = '\0';
}

bool Logger::writeToFile(uint32_t from, uint32_t to, const char* senderName, uint32_t channelHash, const char* message, const char* filename) {
    // Validate inputs
    if (!senderName) senderName = "Unknown";
    if (!message) message = "";
    
    // Sanitize inputs
    char safeSender[64];
    char safeMessage[300];
    char timestamp[32];
    
    String timeStr = getTimeManager()->getTimeString();
    strncpy(timestamp, timeStr.c_str(), sizeof(timestamp) - 1);
    timestamp[sizeof(timestamp) - 1] = '\0';
    
    sanitizeForCSV(senderName, safeSender, sizeof(safeSender));
    sanitizeForCSV(message, safeMessage, sizeof(safeMessage));

    // Format CSV line
    char csvLine[512];
    memset(csvLine, 0, sizeof(csvLine));
    int lineBytes = snprintf(csvLine, sizeof(csvLine), "%s,0x%08x,0x%08x,\"%s\",0x%08x,\"%s\"\n",
             timestamp, from, to, safeSender, channelHash, safeMessage);
    
    if (lineBytes <= 0 || lineBytes >= sizeof(csvLine)) {
        LOG_ERROR("CSV formatting failed");
        return false;
    }
    
    LOG_DEBUG("Writing to %s: %s", filename, safeMessage);
    
    // Open file
    File mainFile = SD.open(filename, FILE_APPEND);
    if (!mainFile) {
        LOG_ERROR("Failed to open %s", filename);
        return false;
    }
    
    // Add header if file is empty
    if (mainFile.size() == 0) {
        size_t headerWritten = mainFile.println("timestamp,from,to,sender_name,channel,message");
        if (headerWritten == 0) {
            LOG_ERROR("Failed to write header to %s", filename);
            mainFile.close();
            return false;
        }
    }
    
    // Write data
    size_t written = mainFile.write((const uint8_t*)csvLine, lineBytes);
    mainFile.flush();
    mainFile.close();
    
    bool success = (written == lineBytes);
    if (success) {
        LOG_DEBUG("Successfully logged to %s", filename);
    } else {
        LOG_ERROR("Write failed to %s: %d/%d bytes", filename, written, lineBytes);
    }
    
    return success;
}