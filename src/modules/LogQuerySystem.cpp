#include "LogQuerySystem.h"
#include <SD.h>
#include "configuration.h"
#include "SimpleTimeManager.h"

const char* LogQuerySystem::MESSAGE_LOG_FILE = "/message_log.csv";
const char* LogQuerySystem::COMMAND_LOG_FILE = "/command_log.csv";

LogQuerySystem::LogQuerySystem() {
    // Don't load on construction - wait for first query
}

// Helper function to parse timestamp string to seconds for comparisons
uint32_t LogQuerySystem::parseTimestampToSeconds(const char* timestamp) {
    if (!timestamp || strlen(timestamp) == 0) return 0;
    
    // Parse "mm/dd/yyyy hh:mm:ss" format
    int month, day, year, hour, minute, second;
    int parsed = sscanf(timestamp, "%d/%d/%d %d:%d:%d", 
                       &month, &day, &year, &hour, &minute, &second);
    
    if (parsed != 6) return 0;
    
    // Simple conversion to seconds (not perfect but good enough for comparisons)
    uint32_t days = 0;
    
    // Add days for complete years since 2024
    for (int y = 2024; y < year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days++; // leap year
    }
    
    // Add days for complete months in current year
    const uint8_t daysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (int m = 1; m < month; m++) {
        days += daysInMonth[m - 1];
        if (m == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            days++; // February in leap year
        }
    }
    
    days += day - 1;
    
    uint32_t seconds = days * 86400UL + hour * 3600UL + minute * 60UL + second;
    return seconds;
}

uint32_t MessageRecord::getTimestampSeconds() const {
    return LogQuerySystem::parseTimestampToSeconds(timestamp);
}

bool LogQuerySystem::isTimestampInRange(const char* timestamp, const char* startTime, const char* endTime) const {
    if (strlen(startTime) == 0 && strlen(endTime) == 0) return true;
    
    uint32_t ts = parseTimestampToSeconds(timestamp);
    uint32_t start = strlen(startTime) > 0 ? parseTimestampToSeconds(startTime) : 0;
    uint32_t end = strlen(endTime) > 0 ? parseTimestampToSeconds(endTime) : UINT32_MAX;
    
    return ts >= start && ts <= end;
}

bool LogQuerySystem::loadMessages() {
    LOG_INFO("Loading all messages into RAM...");
    allMessages.clear();
    allMessages.reserve(200);  // Pre-allocate for efficiency
    
    File file = SD.open(MESSAGE_LOG_FILE, FILE_READ);
    if (!file) {
        LOG_WARN("No message log file found");
        messagesLoaded = true; // Mark as loaded even if empty
        return true;
    }
    
    char line[512];
    bool firstLine = true;
    
    while (file.available()) {
        memset(line, 0, sizeof(line));
        int lineLength = file.readBytesUntil('\n', line, sizeof(line) - 1);
        
        if (lineLength > 0) {
            line[lineLength] = '\0';
            
            // Skip header line
            if (firstLine) {
                firstLine = false;
                continue;
            }
            
            MessageRecord record;
            if (parseCSVLine(line, record)) {
                allMessages.push_back(record);  // File order = chronological order
            }
        }
    }
    
    file.close();
    
    // NO SORTING - file order is already chronological!
    
    messagesLoaded = true;
    LOG_INFO("Loaded %d messages into RAM", allMessages.size());
    return true;
}

bool LogQuerySystem::loadCommands() {
    LOG_INFO("Loading all commands into RAM...");
    allCommands.clear();
    allCommands.reserve(100);  // Pre-allocate for efficiency
    
    File file = SD.open(COMMAND_LOG_FILE, FILE_READ);
    if (!file) {
        LOG_WARN("No command log file found");
        commandsLoaded = true; // Mark as loaded even if empty
        return true;
    }
    
    char line[512];
    bool firstLine = true;
    
    while (file.available()) {
        memset(line, 0, sizeof(line));
        int lineLength = file.readBytesUntil('\n', line, sizeof(line) - 1);
        
        if (lineLength > 0) {
            line[lineLength] = '\0';
            
            // Skip header line
            if (firstLine) {
                firstLine = false;
                continue;
            }
            
            MessageRecord record;
            if (parseCSVLine(line, record)) {
                allCommands.push_back(record);  // File order = chronological order
            }
        }
    }
    
    file.close();
    
    // NO SORTING - file order is already chronological!
    
    commandsLoaded = true;
    LOG_INFO("Loaded %d commands into RAM", allCommands.size());
    return true;
}

void LogQuerySystem::refreshData() {
    messagesLoaded = false;
    commandsLoaded = false;
    loadMessages();
    loadCommands();
}

std::vector<MessageRecord> LogQuerySystem::queryMessages(const QueryFilter& filter) {
    if (!messagesLoaded) {
        if (!loadMessages()) {
            LOG_ERROR("Failed to load messages");
            return std::vector<MessageRecord>();
        }
    }
    
    std::vector<MessageRecord> results;
    results.reserve(filter.maxResults);
    
    for (const auto& record : allMessages) {
        if (matchesFilter(record, filter)) {
            results.push_back(record);
            if (results.size() >= filter.maxResults) break;
        }
    }
    
    return results;
}

std::vector<MessageRecord> LogQuerySystem::queryCommands(const QueryFilter& filter) {
    if (!commandsLoaded) {
        if (!loadCommands()) {
            LOG_ERROR("Failed to load commands");
            return std::vector<MessageRecord>();
        }
    }
    
    std::vector<MessageRecord> results;
    results.reserve(filter.maxResults);
    
    for (const auto& record : allCommands) {
        if (matchesFilter(record, filter)) {
            results.push_back(record);
            if (results.size() >= filter.maxResults) break;
        }
    }
    
    return results;
}

std::vector<MessageRecord> LogQuerySystem::getRecentMessages(int count) {
    if (!messagesLoaded) {
        if (!loadMessages()) {
            LOG_ERROR("Failed to load messages");
            return std::vector<MessageRecord>();
        }
    }
    
    std::vector<MessageRecord> results;
    results.reserve(count);
    
    // Just take the last N messages from the vector (most recent)
    int startIndex = std::max(0, (int)allMessages.size() - count);
    
    for (int i = allMessages.size() - 1; i >= startIndex; i--) {
        results.push_back(allMessages[i]);
    }
    
    return results;
}

std::vector<MessageRecord> LogQuerySystem::getRecentCommands(int count) {
    if (!commandsLoaded) {
        if (!loadCommands()) {
            LOG_ERROR("Failed to load commands");
            return std::vector<MessageRecord>();
        }
    }
    
    std::vector<MessageRecord> results;
    results.reserve(count);
    
    // Just take the last N commands from the vector (most recent)
    int startIndex = std::max(0, (int)allCommands.size() - count);
    
    for (int i = allCommands.size() - 1; i >= startIndex; i--) {
        results.push_back(allCommands[i]);
    }
    
    return results;
}

std::vector<MessageRecord> LogQuerySystem::getMessagesFromUser(uint32_t fromUser, int count) {
    QueryFilter filter;
    filter.fromUser = fromUser;
    filter.maxResults = count;
    return queryMessages(filter);
}

std::vector<MessageRecord> LogQuerySystem::getCommandsFromUser(uint32_t fromUser, int count) {
    QueryFilter filter;
    filter.fromUser = fromUser;
    filter.maxResults = count;
    return queryCommands(filter);
}

std::vector<MessageRecord> LogQuerySystem::getDMsWithUser(uint32_t user, int count) {
    if (!messagesLoaded) {
        if (!loadMessages()) {
            LOG_ERROR("Failed to load messages");
            return std::vector<MessageRecord>();
        }
    }
    
    std::vector<MessageRecord> results;
    results.reserve(count);
    
    // Search backwards through messages (most recent first) for DMs with this user
    for (int i = allMessages.size() - 1; i >= 0 && results.size() < count; i--) {
        const auto& record = allMessages[i];
        if (record.isDM && (record.from == user || record.to == user)) {
            results.push_back(record);
        }
    }
    
    return results;
}

std::vector<MessageRecord> LogQuerySystem::getChannelMessages(uint32_t channelHash, int count) {
    QueryFilter filter;
    filter.channelHash = channelHash;
    filter.maxResults = count;
    return queryMessages(filter);
}

std::vector<MessageRecord> LogQuerySystem::getTodaysMessages() {
    // Get today's date from timeManager
    String todayStr = getTimeManager()->getTimeString();
    char today[32];
    strncpy(today, todayStr.c_str(), sizeof(today) - 1);
    today[sizeof(today) - 1] = '\0';
    
    // Extract just the date part (mm/dd/yyyy)
    char* space = strchr(today, ' ');
    if (space) *space = '\0';
    
    QueryFilter filter;
    snprintf(filter.startTime, sizeof(filter.startTime), "%s 00:00:00", today);
    snprintf(filter.endTime, sizeof(filter.endTime), "%s 23:59:59", today);
    filter.maxResults = 100;
    return queryMessages(filter);
}

std::vector<MessageRecord> LogQuerySystem::getTodaysCommands() {
    // Get today's date from timeManager
    String todayStr = getTimeManager()->getTimeString();
    char today[32];
    strncpy(today, todayStr.c_str(), sizeof(today) - 1);
    today[sizeof(today) - 1] = '\0';
    
    // Extract just the date part (mm/dd/yyyy)
    char* space = strchr(today, ' ');
    if (space) *space = '\0';
    
    QueryFilter filter;
    snprintf(filter.startTime, sizeof(filter.startTime), "%s 00:00:00", today);
    snprintf(filter.endTime, sizeof(filter.endTime), "%s 23:59:59", today);
    filter.maxResults = 100;
    return queryCommands(filter);
}

std::vector<MessageRecord> LogQuerySystem::searchMessages(const char* searchText, int count) {
    QueryFilter filter;
    strncpy(filter.searchText, searchText, sizeof(filter.searchText) - 1);
    filter.maxResults = count;
    return queryMessages(filter);
}

std::vector<MessageRecord> LogQuerySystem::searchCommands(const char* searchText, int count) {
    QueryFilter filter;
    strncpy(filter.searchText, searchText, sizeof(filter.searchText) - 1);
    filter.maxResults = count;
    return queryCommands(filter);
}

bool LogQuerySystem::parseCSVLine(const char* line, MessageRecord& record) {
    // Parse: timestamp,from,to,sender_name,channel,message
    const char* ptr = line;
    char field[512];
    int fieldIndex = 0;
    
    // Clear the record
    memset(&record, 0, sizeof(record));
    
    while (*ptr && fieldIndex < 6) {
        int fieldLen = 0;
        
        // Skip leading whitespace
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        
        if (*ptr == '"') {
            // Quoted field
            ptr++; // Skip opening quote
            while (*ptr && fieldLen < sizeof(field) - 1) {
                if (*ptr == '"') {
                    if (*(ptr + 1) == '"') {
                        // Escaped quote
                        field[fieldLen++] = '"';
                        ptr += 2;
                    } else {
                        // End of quoted field
                        ptr++;
                        break;
                    }
                } else {
                    field[fieldLen++] = *ptr++;
                }
            }
        } else {
            // Unquoted field - read until comma or end
            while (*ptr && *ptr != ',' && fieldLen < sizeof(field) - 1) {
                field[fieldLen++] = *ptr++;
            }
        }
        
        field[fieldLen] = '\0';
        
        // Process the field based on index
        switch (fieldIndex) {
            case 0: // timestamp
                strncpy(record.timestamp, field, sizeof(record.timestamp) - 1);
                record.timestamp[sizeof(record.timestamp) - 1] = '\0';
                break;
            case 1: // from
                record.from = strtoul(field, nullptr, 16);
                break;
            case 2: // to
                record.to = strtoul(field, nullptr, 16);
                record.isDM = (record.to != 0);
                break;
            case 3: // sender_name
                strncpy(record.senderName, field, sizeof(record.senderName) - 1);
                record.senderName[sizeof(record.senderName) - 1] = '\0';
                break;
            case 4: // channel
                record.channelHash = strtoul(field, nullptr, 16);
                break;
            case 5: // message
                strncpy(record.message, field, sizeof(record.message) - 1);
                record.message[sizeof(record.message) - 1] = '\0';
                break;
        }
        
        fieldIndex++;
        
        // Skip to next field
        if (*ptr == ',') {
            ptr++;
        }
    }
    
    return fieldIndex >= 6; // Must have all 6 fields
}

bool LogQuerySystem::matchesFilter(const MessageRecord& record, const QueryFilter& filter) {
    // Time range check
    if (!isTimestampInRange(record.timestamp, filter.startTime, filter.endTime)) return false;
    
    // User filters
    if (filter.fromUser != 0 && record.from != filter.fromUser) return false;
    if (filter.toUser != 0 && record.to != filter.toUser) return false;
    
    // Channel filter
    if (filter.channelHash != 255 && record.channelHash != filter.channelHash) return false;
    
    // DM/Broadcast filters
    if (filter.onlyDMs && !record.isDM) return false;
    if (filter.onlyBroadcast && record.isDM) return false;
    
    // Text search
    if (strlen(filter.searchText) > 0) {
        // Case-insensitive search in message text
        char lowerMessage[256], lowerSearch[64];
        strncpy(lowerMessage, record.message, sizeof(lowerMessage) - 1);
        strncpy(lowerSearch, filter.searchText, sizeof(lowerSearch) - 1);
        lowerMessage[sizeof(lowerMessage) - 1] = '\0';
        lowerSearch[sizeof(lowerSearch) - 1] = '\0';
        
        // Convert to lowercase
        for (int i = 0; lowerMessage[i]; i++) lowerMessage[i] = tolower(lowerMessage[i]);
        for (int i = 0; lowerSearch[i]; i++) lowerSearch[i] = tolower(lowerSearch[i]);
        
        if (!strstr(lowerMessage, lowerSearch)) return false;
    }
    
    return true;
}

LogQuerySystem::LogStats LogQuerySystem::getLogStatistics() {
    LogStats stats = {};
    memset(stats.oldestTimestamp, 0, sizeof(stats.oldestTimestamp));
    memset(stats.newestTimestamp, 0, sizeof(stats.newestTimestamp));
    
    if (!messagesLoaded) loadMessages();
    if (!commandsLoaded) loadCommands();
    
    // Simple array to track unique users
    uint32_t uniqueUsers[100];
    int uniqueUserCount = 0;
    
    // Process messages
    for (const auto& record : allMessages) {
        stats.totalMessages++;
        if (record.isDM) stats.totalDMs++;
        else stats.totalBroadcasts++;
        
        // Track unique users
        bool found = false;
        for (int i = 0; i < uniqueUserCount; i++) {
            if (uniqueUsers[i] == record.from) {
                found = true;
                break;
            }
        }
        
        if (!found && uniqueUserCount < 100) {
            uniqueUsers[uniqueUserCount++] = record.from;
        }
        
        // Update oldest/newest timestamps
        if (strlen(stats.oldestTimestamp) == 0 || 
            record.getTimestampSeconds() < parseTimestampToSeconds(stats.oldestTimestamp)) {
            strncpy(stats.oldestTimestamp, record.timestamp, sizeof(stats.oldestTimestamp) - 1);
            stats.oldestTimestamp[sizeof(stats.oldestTimestamp) - 1] = '\0';
        }
        if (strlen(stats.newestTimestamp) == 0 ||
            record.getTimestampSeconds() > parseTimestampToSeconds(stats.newestTimestamp)) {
            strncpy(stats.newestTimestamp, record.timestamp, sizeof(stats.newestTimestamp) - 1);
            stats.newestTimestamp[sizeof(stats.newestTimestamp) - 1] = '\0';
        }
    }
    
    // Process commands
    for (const auto& record : allCommands) {
        stats.totalCommands++;
        
        // Track unique users
        bool found = false;
        for (int i = 0; i < uniqueUserCount; i++) {
            if (uniqueUsers[i] == record.from) {
                found = true;
                break;
            }
        }
        
        if (!found && uniqueUserCount < 100) {
            uniqueUsers[uniqueUserCount++] = record.from;
        }
        
        // Update timestamps
        if (strlen(stats.oldestTimestamp) == 0 || 
            record.getTimestampSeconds() < parseTimestampToSeconds(stats.oldestTimestamp)) {
            strncpy(stats.oldestTimestamp, record.timestamp, sizeof(stats.oldestTimestamp) - 1);
            stats.oldestTimestamp[sizeof(stats.oldestTimestamp) - 1] = '\0';
        }
        if (strlen(stats.newestTimestamp) == 0 ||
            record.getTimestampSeconds() > parseTimestampToSeconds(stats.newestTimestamp)) {
            strncpy(stats.newestTimestamp, record.timestamp, sizeof(stats.newestTimestamp) - 1);
            stats.newestTimestamp[sizeof(stats.newestTimestamp) - 1] = '\0';
        }
    }
    
    stats.uniqueUsers = uniqueUserCount;
    return stats;
}