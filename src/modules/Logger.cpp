// Logger.cpp - Implementation of the buffered logging system
#include "Logger.h"
#include "FSCommon.h"
#include "configuration.h"
#include <SD.h>

BufferedLogger bufferedLogger;

BufferedLogger::BufferedLogger() {
    messageBuffer.reserve(MESSAGE_BUFFER_SIZE);
    telemetryBuffer.reserve(TELEMETRY_BUFFER_SIZE);
}

bool BufferedLogger::addMessageLog(uint32_t from, uint32_t to, const char* senderName, uint8_t channelIndex, const char* message) {
    // Check if immediate write mode is enabled
    if (immediateMessageWrite) {
        return writeMessageToSDCard(from, to, senderName, channelIndex, message);
    }

    // Original buffering logic
    // Check if buffer is approaching threshold and should be flushed
    if (messageBuffer.size() >= (MESSAGE_BUFFER_SIZE * MESSAGE_FLUSH_THRESHOLD / 100)) {
        flushToSDCard();
    }
    
    // If buffer is completely full after flush attempt, drop oldest message
    if (messageBuffer.size() >= MESSAGE_BUFFER_SIZE) {
        messageBuffer.erase(messageBuffer.begin());
    }
    
    MessageLogEntry entry;
    entry.timestamp = millis();
    entry.from = from;
    entry.to = to;
    strncpy(entry.senderName, senderName ? senderName : "Unknown", sizeof(entry.senderName) - 1);
    entry.senderName[sizeof(entry.senderName) - 1] = '\0';
    entry.channelIndex = channelIndex;
    strncpy(entry.message, message, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    messageBuffer.push_back(entry);
    return true;
}

bool BufferedLogger::addTelemetryLog(int16_t current_ma, int16_t voltage_mv) {
    // Check if immediate write mode is enabled
    if (immediateTelemetryWrite) {
        return writeTelemetryToSDCard(current_ma, voltage_mv);
    }

    // Original buffering logic
    // Check if buffer is approaching threshold and should be flushed
    if (telemetryBuffer.size() >= (TELEMETRY_BUFFER_SIZE * TELEMETRY_FLUSH_THRESHOLD / 100)) {
        flushToSDCard();
    }

    // If buffer is completely full after flush attempt, drop oldest reading
    if (telemetryBuffer.size() >= TELEMETRY_BUFFER_SIZE) {
        telemetryBuffer.erase(telemetryBuffer.begin());
    }

    TelemetryLogEntry entry;
    entry.timestamp = millis();
    entry.current_ma = current_ma;
    entry.voltage_mv = voltage_mv;

    telemetryBuffer.push_back(entry);
    return true;
}

bool BufferedLogger::writeMessageToSDCard(uint32_t from, uint32_t to, const char* senderName, uint8_t channelIndex, const char* message) {
    LOG_INFO("Writing single message to SD card immediately");

    // Power on the SD card
    sdPowerOn();
    delay(100); // Give SD card time to initialize

    // Setup SD card
    setupSDCard();

    bool success = false;

    char logfile_name[32];
    snprintf(logfile_name, sizeof(logfile_name), "/message_log.csv");

    File dataFile = SD.open(logfile_name, FILE_APPEND);
    if (dataFile) {
        // Write header if file is new (size 0)
        if (dataFile.size() == 0) {
            dataFile.println("timestamp,from,to,sender_name,channel,message");
        }

        // Write the single message
        dataFile.printf("%lu,0x%08x,0x%08x,%s,%d,%s\n",
            millis(),
            from,
            to,
            senderName ? senderName : "Unknown",
            channelIndex,
            message);

        dataFile.close();
        LOG_INFO("Wrote message to SD card: %s", message);
        success = true;
    } else {
        LOG_ERROR("Failed to open message log file");
    }

    // Power off the SD card
    sdPowerOff();

    return success;
}

bool BufferedLogger::writeTelemetryToSDCard(int16_t current_ma, int16_t voltage_mv) {
    LOG_INFO("Writing single telemetry reading to SD card immediately");

    // Power on the SD card
    sdPowerOn();
    delay(100); // Give SD card time to initialize

    // Setup SD card
    setupSDCard();

    bool success = false;

    char logfile_name[32];
    snprintf(logfile_name, sizeof(logfile_name), "/telemetry_log.csv");

    File dataFile = SD.open(logfile_name, FILE_APPEND);
    if (dataFile) {
        // Write header if file is new (size 0)
        if (dataFile.size() == 0) {
            dataFile.println("timestamp,current_ma,voltage_mv");
        }

        // Write the single telemetry reading
        dataFile.printf("%lu,%d,%d\n",
            millis(),
            current_ma,
            voltage_mv);

        dataFile.close();
        LOG_INFO("Wrote telemetry to SD card: %dmA, %dmV", current_ma, voltage_mv);
        success = true;
    } else {
        LOG_ERROR("Failed to open telemetry log file");
    }

    // Power off the SD card
    sdPowerOff();

    return success;
}

bool BufferedLogger::forceFlush() {
    return flushToSDCard();
}

bool BufferedLogger::flushToSDCard() {
    if (messageBuffer.empty() && telemetryBuffer.empty()) {
        return true; // Nothing to flush
    }

    LOG_INFO("Flushing log buffers to SD card. Messages: %d/%d, Telemetry: %d/%d", 
        messageBuffer.size(), MESSAGE_BUFFER_SIZE,
        telemetryBuffer.size(), TELEMETRY_BUFFER_SIZE);

    // Power on the SD card
    sdPowerOn();
    delay(100); // Give SD card time to initialize

    // Setup SD card
    setupSDCard();

    bool success = true;

    // Write message logs
    if (!messageBuffer.empty()) {
        char logfile_name[32];
        snprintf(logfile_name, sizeof(logfile_name), "/message_log.csv");

        File dataFile = SD.open(logfile_name, FILE_APPEND);
        if (dataFile) {
            // Write header if file is new (size 0)
            if (dataFile.size() == 0) {
                dataFile.println("timestamp,from,to,sender_name,channel,message");
            }

            
            // Write all buffered messages
            
            for (const auto& entry : messageBuffer) {
                //const char* messageType = (entry.to == 0) ? "CHANNEL" : "DM";
                dataFile.printf("%lu,0x%08x,0x%08x,%s,%d,%s\n",
                    entry.timestamp,
                    entry.from,
                    entry.to,
                    entry.senderName,
                    entry.channelIndex,
                    entry.message);
            }
            dataFile.close();
            LOG_INFO("Wrote %d messages to SD card", messageBuffer.size());
            messageBuffer.clear();
        } else {
            LOG_ERROR("Failed to open message log file");
            success = false;
        }
    }

    // Write telemetry logs
    if (!telemetryBuffer.empty()) {
        char logfile_name[32];
        snprintf(logfile_name, sizeof(logfile_name), "/telemetry_log.csv");
        
        File dataFile = SD.open(logfile_name, FILE_APPEND);
        if (dataFile) {
            // Write header if file is new (size 0)
            if (dataFile.size() == 0) {
                dataFile.println("timestamp,current_ma,voltage_mv");
            }

            // Write all buffered telemetry readings
            for (const auto& entry : telemetryBuffer) {
                dataFile.printf("%lu,%d,%d\n",
                    entry.timestamp,
                    entry.current_ma,
                    entry.voltage_mv);
            }
            dataFile.close();
            LOG_INFO("Wrote %d telemetry readings to SD card", telemetryBuffer.size());
            telemetryBuffer.clear();
        } else {
            LOG_ERROR("Failed to open telemetry log file");
            success = false;
        }
    }

    // Power off the SD card
    sdPowerOff();

    return success;
}