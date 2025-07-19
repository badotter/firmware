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

void BufferedLogger::sanitizeForCSV(const char* input, char* output, size_t maxLen) {
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

bool BufferedLogger::atomicWriteMessage(uint32_t from, uint32_t to, const char* senderName, uint8_t channelIndex, const char* message) {
    // Sanitize inputs
    char safeSender[64];
    char safeMessage[300];
    sanitizeForCSV(senderName, safeSender, sizeof(safeSender));
    sanitizeForCSV(message, safeMessage, sizeof(safeMessage));

    // Format the complete CSV line
    char csvLine[512];
    snprintf(csvLine, sizeof(csvLine), "%lu,0x%08x,0x%08x,\"%s\",%d,\"%s\"\n",
             millis(), from, to, safeSender, channelIndex, safeMessage);

    // Step 1: Write to temporary file
    File tempFile = SD.open("/msg_temp.csv", FILE_WRITE);
    if (!tempFile) {
        LOG_ERROR("Failed to create temp file");
        return false;
    }

    size_t written = tempFile.print(csvLine);
    tempFile.flush();
    tempFile.close();

    // Step 2: Verify the write by reading it back
    tempFile = SD.open("/msg_temp.csv", FILE_READ);
    if (!tempFile) {
        LOG_ERROR("Failed to verify temp file");
        SD.remove("/msg_temp.csv");
        return false;
    }

    String readBack = tempFile.readString();
    tempFile.close();

    // Step 3: Check if what we read matches what we wrote
    if (readBack.length() != written || !readBack.equals(csvLine)) {
        LOG_ERROR("Temp file verification failed");
        SD.remove("/msg_temp.csv");
        return false;
    }

    // Step 4: Append verified data to main log file
    File mainFile = SD.open("/message_log.csv", FILE_APPEND);
    if (!mainFile) {
        LOG_ERROR("Failed to open main log file");
        SD.remove("/msg_temp.csv");
        return false;
    }

    // Add header if file is new
    if (mainFile.size() == 0) {
        mainFile.println("timestamp,from,to,sender_name,channel,message");
    }

    mainFile.print(csvLine);
    mainFile.flush();
    mainFile.close();

    // Step 5: Clean up temp file
    SD.remove("/msg_temp.csv");

    LOG_DEBUG("Message logged safely: %s", safeMessage);
    return true;
}

bool BufferedLogger::writeMessageToSDCard(uint32_t from, uint32_t to, const char* senderName, uint8_t channelIndex, const char* message) {
    LOG_INFO("Writing single message to SD card safely");

    // Power on the SD card
    sdPowerOn();
    delay(100);
    setupSDCard();

    bool success = false;

    // Use atomic write instead of direct write
    bool success = atomicWriteMessage(from, 0xffffffff, senderName, channelIndex, message);

    if (success) {
        LOG_INFO("Message logged safely: %s", message);
    } else {
        LOG_ERROR("Failed to log message safely");
    }

    sdPowerOff();

    return success;
}

    /*
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
    */

bool BufferedLogger::atomicWriteTelemetry(int16_t current_ma, int16_t voltage_mv) {
    // Format the complete CSV line
    char csvLine[128];
    snprintf(csvLine, sizeof(csvLine), "%lu,%d,%d\n",
            millis(), current_ma, voltage_mv);

    // Step 1: Write to temporary file
    File tempFile = SD.open("/telem_temp.csv", FILE_WRITE);
    if (!tempFile) {
        LOG_ERROR("Failed to create telemetry temp file");
        return false;
    }

    size_t written = tempFile.print(csvLine);
    tempFile.flush();
    tempFile.close();

    // Step 2: Verify the write by reading it back
    tempFile = SD.open("/telem_temp.csv", FILE_READ);
    if (!tempFile) {
        LOG_ERROR("Failed to verify telemetry temp file");
        SD.remove("/telem_temp.csv");
        return false;
    }

    String readBack = tempFile.readString();
    tempFile.close();

    // Step 3: Check if what we read matches what we wrote
    if (readBack.length() != written || !readBack.equals(csvLine)) {
        LOG_ERROR("Telemetry temp file verification failed");
        SD.remove("/telem_temp.csv");
        return false;
    }

    // Step 4: Append verified data to main telemetry log file
    File mainFile = SD.open("/telemetry_log.csv", FILE_APPEND);
    if (!mainFile) {
        LOG_ERROR("Failed to open main telemetry log file");
        SD.remove("/telem_temp.csv");
        return false;
    }

    // Add header if file is new
    if (mainFile.size() == 0) {
        mainFile.println("timestamp,current_ma,voltage_mv");
    }

    mainFile.print(csvLine);
    mainFile.flush();
    mainFile.close();

    // Step 5: Clean up temp file
    SD.remove("/telem_temp.csv");

    LOG_DEBUG("Telemetry logged safely: %dmA, %dmV", current_ma, voltage_mv);
    return true;
}

bool BufferedLogger::writeTelemetryToSDCard(int16_t current_ma, int16_t voltage_mv) {
    LOG_INFO("Writing single telemetry reading to SD card safely");

    // Power on the SD card
    sdPowerOn();
    delay(100);
    setupSDCard();

    // Use atomic write instead of direct write
    bool success = atomicWriteTelemetry(current_ma, voltage_mv);

    if (success) {
        LOG_INFO("Telemetry logged safely: %dmA, %dmV", current_ma, voltage_mv);
    } else {
        LOG_ERROR("Failed to log telemetry safely");
    }

    // Power off the SD card
    sdPowerOff();
    return success;
}

/*
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
}*/


bool BufferedLogger::forceFlush() {
    return flushToSDCard();
}

bool BufferedLogger::flushToSDCard() {
    if (messageBuffer.empty() && telemetryBuffer.empty()) {
        return true;
    }

    LOG_INFO("Flushing log buffers to SD card safely");

    // Power on the SD card
    sdPowerOn();
    delay(100);
    setupSDCard();

    bool success = true;

    // Write message logs using atomic writes
    if (!messageBuffer.empty()) {
        for (const auto& entry : messageBuffer) {
            if (!atomicWriteMessage(entry.from, entry.to, entry.senderName,entry.channelIndex, entry.message)) {
                LOG_ERROR("Failed to write message entry");
                success = false;
                break; // Stop on first failure to prevent corruption
            }
        }

        if (success) {
            messageBuffer.clear();
            LOG_INFO("Flushed %d messages safely", messageBuffer.size());
        }
    }

    // Telemetry can use simpler atomic write (less corruption risk)
    if (success && !telemetryBuffer.empty()) {
        // Build all telemetry data first
        String telemetryData = "";
        for (const auto& entry : telemetryBuffer) {
            char line[128];
            snprintf(line, sizeof(line), "%lu,%d,%d\n",
                    entry.timestamp, entry.current_ma, entry.voltage_mv);
            telemetryData += line;
        }

        // Write telemetry atomically
        File tempFile = SD.open("/telem_temp.csv", FILE_WRITE);
        if (tempFile) {
            tempFile.print(telemetryData);
            tempFile.close();

            // Append to main telemetry file
            File mainFile = SD.open("/telemetry_log.csv", FILE_APPEND);
            if (mainFile) {
                if (mainFile.size() == 0) {
                    mainFile.println("timestamp,current_ma,voltage_mv");
                }
                mainFile.print(telemetryData);
                mainFile.close();

                telemetryBuffer.clear();
                LOG_INFO("Flushed %d telemetry readings", telemetryBuffer.size());
            }
            SD.remove("/telem_temp.csv");
        }
    }

    sdPowerOff();
    return success;
}

/*

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
}*/