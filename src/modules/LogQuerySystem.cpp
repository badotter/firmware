#include "LogQuerySystem.h"
#include <SD.h>
#include "configuration.h"
#include "SimpleTimeManager.h"

extern SimpleTimeManager timeManager;

const char* LogQuerySystem::MESSAGE_LOG_FILE = "/message_log.csv";
const char* LogQuerySystem::COMMAND_LOG_FILE = "/command_log.csv";
const char* LogQuerySystem::INDEX_FILE = "/msg_index.dat";

LogQuerySystem::LogQuerySystem() {
    loadIndex();
    loadCommandIndex();
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
    // Days since Jan 1, 2024
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

uint32_t LogQuerySystem::IndexEntry::getTimestampSeconds() const {
    return LogQuerySystem::parseTimestampToSeconds(timestamp);
}

bool LogQuerySystem::isTimestampInRange(const char* timestamp, const char* startTime, const char* endTime) const {
    if (strlen(startTime) == 0 && strlen(endTime) == 0) return true;
    
    uint32_t ts = parseTimestampToSeconds(timestamp);
    uint32_t start = strlen(startTime) > 0 ? parseTimestampToSeconds(startTime) : 0;
    uint32_t end = strlen(endTime) > 0 ? parseTimestampToSeconds(endTime) : UINT32_MAX;
    
    return ts >= start && ts <= end;
}

// New helper function to read records from end of file backwards
std::vector<MessageRecord> LogQuerySystem::readRecentRecords(const char* filename, int maxCount) {
    std::vector<MessageRecord> records;
    
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        LOG_ERROR("Failed to open %s", filename);
        return records;
    }
    
    // Find the end of file
    file.seek(0, SeekEnd);
    uint32_t fileSize = file.position();
    
    if (fileSize == 0) {
        file.close();
        return records;
    }
    
    // Read backwards line by line
    std::vector<String> lines;
    String currentLine = "";
    bool inHeader = true;
    
    // Start from end and work backwards
    for (uint32_t pos = fileSize - 1; pos > 0 && lines.size() < maxCount + 10; pos--) {
        file.seek(pos);
        char c = file.read();
        
        if (c == '\n' || pos == 1) {
            if (currentLine.length() > 0) {
                // Skip header line (it will be the last line we encounter when reading backwards)
                if (currentLine.indexOf("timestamp,from,to") >= 0) {
                    break; // We've reached the header
                }
                lines.push_back(currentLine);
                currentLine = "";
                
                if (lines.size() >= maxCount + 10) break; // Get some extra for filtering
            }
        } else if (c != '\r') {
            currentLine = String(c) + currentLine;
        }
    }
    
    file.close();
    
    // Parse the lines (they're already in reverse chronological order)
    for (const auto& line : lines) {
        if (records.size() >= maxCount) break;
        
        MessageRecord record;
        if (parseCSVLine(line.c_str(), record)) {
            records.push_back(record);
        }
    }
    
    return records;
}

std::vector<MessageRecord> LogQuerySystem::queryMessages(const QueryFilter& filter) {
    std::vector<MessageRecord> results;
    
    // For recent queries without specific time ranges, use the faster backwards read
    if (strlen(filter.startTime) == 0 && strlen(filter.endTime) == 0 && 
        filter.fromUser == 0 && filter.toUser == 0 && strlen(filter.searchText) == 0 &&
        !filter.onlyDMs && !filter.onlyBroadcast) {
        return readRecentRecords(MESSAGE_LOG_FILE, filter.maxResults);
    }
    
    // For filtered queries, use the index-based approach but without sorting
    if (!indexLoaded) {
        if (!loadIndex()) {
            LOG_ERROR("Failed to load message index");
            return results;
        }
    }
    
    // Filter using index (index is already in file order, newest first)
    std::vector<uint32_t> candidatePositions;
    
    for (const auto& entry : messageIndex) {
        if (!isTimestampInRange(entry.timestamp, filter.startTime, filter.endTime)) continue;
        if (filter.fromUser != 0 && entry.from != filter.fromUser) continue;
        if (filter.channelHash != 255 && entry.channelHash != filter.channelHash) continue;
        if (filter.onlyDMs && !entry.isDM) continue;
        if (filter.onlyBroadcast && entry.isDM) continue;
        
        candidatePositions.push_back(entry.filePosition);
        
        if (candidatePositions.size() >= filter.maxResults * 2) break; // Get extra for text filtering
    }
    
    // Read the actual records and apply remaining filters
    auto records = readRecordsAtPositions(candidatePositions);
    
    for (const auto& record : records) {
        if (matchesFilter(record, filter)) {
            results.push_back(record);
            if (results.size() >= filter.maxResults) break;
        }
    }
    
    return results;
}

std::vector<MessageRecord> LogQuerySystem::queryCommands(const QueryFilter& filter) {
    std::vector<MessageRecord> results;
    
    // For recent queries without specific time ranges, use the faster backwards read
    if (strlen(filter.startTime) == 0 && strlen(filter.endTime) == 0 && 
        filter.fromUser == 0 && filter.toUser == 0 && strlen(filter.searchText) == 0 &&
        !filter.onlyDMs && !filter.onlyBroadcast) {
        return readRecentRecords(COMMAND_LOG_FILE, filter.maxResults);
    }
    
    if (!commandIndexLoaded) {
        if (!loadCommandIndex()) {
            LOG_ERROR("Failed to load command index");
            return results;
        }
    }
    
    // Filter using command index (index is already in file order, newest first)
    std::vector<uint32_t> candidatePositions;
    
    for (const auto& entry : commandIndex) {
        if (!isTimestampInRange(entry.timestamp, filter.startTime, filter.endTime)) continue;
        if (filter.fromUser != 0 && entry.from != filter.fromUser) continue;
        if (filter.channelHash != 255 && entry.channelHash != filter.channelHash) continue;
        if (filter.onlyDMs && !entry.isDM) continue;
        if (filter.onlyBroadcast && entry.isDM) continue;
        
        candidatePositions.push_back(entry.filePosition);
        
        if (candidatePositions.size() >= filter.maxResults * 2) break; // Get extra for text filtering
    }
    
    // Read the actual command records and apply remaining filters
    auto records = readCommandsAtPositions(candidatePositions);
    
    for (const auto& record : records) {
        if (matchesFilter(record, filter)) {
            results.push_back(record);
            if (results.size() >= filter.maxResults) break;
        }
    }
    
    return results;
}

std::vector<MessageRecord> LogQuerySystem::getRecentMessages(int count) {
    QueryFilter filter;
    filter.maxResults = count;
    return queryMessages(filter); // This will use the fast backwards read
}

std::vector<MessageRecord> LogQuerySystem::getRecentCommands(int count) {
    QueryFilter filter;
    filter.maxResults = count;
    return queryCommands(filter); // This will use the fast backwards read
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
    QueryFilter filter;
    filter.onlyDMs = true;
    filter.maxResults = count;
    
    // Get messages both from and to this user
    auto fromUser = queryMessages(filter);
    filter.fromUser = 0;
    filter.toUser = user;
    auto toUser = queryMessages(filter);
    
    // Merge results (both are already in file order, newest first)
    std::vector<MessageRecord> combined;
    combined.insert(combined.end(), fromUser.begin(), fromUser.end());
    combined.insert(combined.end(), toUser.begin(), toUser.end());
    
    // Remove duplicates and limit results (keep file order)
    std::vector<MessageRecord> result;
    for (const auto& record : combined) {
        if (result.size() >= count) break;
        
        bool isDuplicate = false;
        for (const auto& existing : result) {
            if (strcmp(existing.timestamp, record.timestamp) == 0 && 
                existing.from == record.from) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) {
            result.push_back(record);
        }
    }
    
    return result;
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

// Fixed CSV parsing to handle quoted fields properly
bool LogQuerySystem::parseCSVLine(const char* line, MessageRecord& record) {
    // Parse: timestamp,from,to,sender_name,channel,message
    // Handle quoted fields properly
    
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
    if (filter.toUser != 0 && record.to != filter.toUser) return false;
    
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

std::vector<MessageRecord> LogQuerySystem::readRecordsAtPositions(const std::vector<uint32_t>& positions) {
    std::vector<MessageRecord> records;
    
    File file = SD.open(MESSAGE_LOG_FILE, FILE_READ);
    if (!file) {
        LOG_ERROR("Failed to open message log file");
        return records;
    }
    
    char line[512];
    for (uint32_t pos : positions) {
        memset(line, 0, sizeof(line));
        file.seek(pos);
        if (file.readBytesUntil('\n', line, sizeof(line) - 1) > 0) {
            line[sizeof(line) - 1] = '\0';
            
            MessageRecord record;
            if (parseCSVLine(line, record)) {
                records.push_back(record);
            }
        }
    }
    
    file.close();
    return records;
}

std::vector<MessageRecord> LogQuerySystem::readCommandsAtPositions(const std::vector<uint32_t>& positions) {
    std::vector<MessageRecord> records;
    
    File file = SD.open(COMMAND_LOG_FILE, FILE_READ);
    if (!file) {
        LOG_ERROR("Failed to open command log file");
        return records;
    }
    
    char line[512];
    for (uint32_t pos : positions) {
        memset(line, 0, sizeof(line));
        file.seek(pos);
        if (file.readBytesUntil('\n', line, sizeof(line) - 1) > 0) {
            line[sizeof(line) - 1] = '\0';
            
            MessageRecord record;
            if (parseCSVLine(line, record)) {
                records.push_back(record);
            }
        }
    }
    
    file.close();
    return records;
}

bool LogQuerySystem::loadIndex() {
    // This would load a binary index file for faster searches
    // For now, we'll rebuild it each time (or you could implement caching)
    return rebuildIndex();
}

bool LogQuerySystem::loadCommandIndex() {
    // This would load a binary command index file for faster searches
    // For now, we'll rebuild it each time (or you could implement caching)
    return rebuildCommandIndex();
}

bool LogQuerySystem::rebuildIndex() {
    LOG_INFO("Rebuilding message index...");
    messageIndex.clear();
    
    File file = SD.open(MESSAGE_LOG_FILE, FILE_READ);
    if (!file) {
        LOG_WARN("No message log file found");
        indexLoaded = true; // Mark as loaded even if empty
        return true;
    }
    
    uint32_t position = 0;
    char line[512];
    bool firstLine = true;
    
    while (file.available()) {
        memset(line, 0, sizeof(line));
        position = file.position();
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
                IndexEntry entry;
                strncpy(entry.timestamp, record.timestamp, sizeof(entry.timestamp) - 1);
                entry.timestamp[sizeof(entry.timestamp) - 1] = '\0';
                entry.filePosition = position;
                entry.from = record.from;
                entry.channelHash = record.channelHash;
                entry.isDM = record.isDM;
                
                messageIndex.push_back(entry);
            }
        }
    }
    
    file.close();
    
    // DON'T sort - keep in file order (oldest first in index)
    // When we iterate through index, we'll go backwards for recent queries
    
    indexLoaded = true;
    LOG_INFO("Message index rebuilt with %d entries", messageIndex.size());
    return true;
}

bool LogQuerySystem::rebuildCommandIndex() {
    LOG_INFO("Rebuilding command index...");
    commandIndex.clear();
    
    File file = SD.open(COMMAND_LOG_FILE, FILE_READ);
    if (!file) {
        LOG_WARN("No command log file found");
        commandIndexLoaded = true; // Mark as loaded even if empty
        return true;
    }
    
    uint32_t position = 0;
    char line[512];
    bool firstLine = true;
    
    while (file.available()) {
        memset(line, 0, sizeof(line));
        position = file.position();
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
                IndexEntry entry;
                strncpy(entry.timestamp, record.timestamp, sizeof(entry.timestamp) - 1);
                entry.timestamp[sizeof(entry.timestamp) - 1] = '\0';
                entry.filePosition = position;
                entry.from = record.from;
                entry.channelHash = record.channelHash;
                entry.isDM = record.isDM;
                
                commandIndex.push_back(entry);
            }
        }
    }
    
    file.close();
    
    // DON'T sort - keep in file order (oldest first in index)
    // When we iterate through index, we'll go backwards for recent queries
    
    commandIndexLoaded = true;
    LOG_INFO("Command index rebuilt with %d entries", commandIndex.size());
    return true;
}

LogQuerySystem::LogStats LogQuerySystem::getLogStatistics() {
    LogStats stats = {};
    memset(stats.oldestTimestamp, 0, sizeof(stats.oldestTimestamp));
    memset(stats.newestTimestamp, 0, sizeof(stats.newestTimestamp));
    
    if (!indexLoaded) {
        loadIndex();
    }
    if (!commandIndexLoaded) {
        loadCommandIndex();
    }
    
    // Simple array to track unique users (instead of std::set)
    uint32_t uniqueUsers[100];  // Adjust size as needed
    int uniqueUserCount = 0;
    
    // Process message index (in file order, so first = oldest, last = newest)
    if (!messageIndex.empty()) {
        strncpy(stats.oldestTimestamp, messageIndex.front().timestamp, sizeof(stats.oldestTimestamp) - 1);
        stats.oldestTimestamp[sizeof(stats.oldestTimestamp) - 1] = '\0';
        strncpy(stats.newestTimestamp, messageIndex.back().timestamp, sizeof(stats.newestTimestamp) - 1);
        stats.newestTimestamp[sizeof(stats.newestTimestamp) - 1] = '\0';
    }
    
    for (const auto& entry : messageIndex) {
        stats.totalMessages++;
        if (entry.isDM) stats.totalDMs++;
        else stats.totalBroadcasts++;
        
        // Check if user is already in our list
        bool found = false;
        for (int i = 0; i < uniqueUserCount; i++) {
            if (uniqueUsers[i] == entry.from) {
                found = true;
                break;
            }
        }
        
        // Add new user if not found and we have space
        if (!found && uniqueUserCount < 100) {
            uniqueUsers[uniqueUserCount++] = entry.from;
        }
    }
    
    // Process command index
    for (const auto& entry : commandIndex) {
        stats.totalCommands++;
        
        // Check if user is already in our list
        bool found = false;
        for (int i = 0; i < uniqueUserCount; i++) {
            if (uniqueUsers[i] == entry.from) {
                found = true;
                break;
            }
        }
        
        // Add new user if not found and we have space
        if (!found && uniqueUserCount < 100) {
            uniqueUsers[uniqueUserCount++] = entry.from;
        }
        
        // Update oldest/newest if commands extend the range
        if (!commandIndex.empty()) {
        if (strlen(stats.oldestTimestamp) == 0 || 
                parseTimestampToSeconds(commandIndex.front().timestamp) < parseTimestampToSeconds(stats.oldestTimestamp)) {
                strncpy(stats.oldestTimestamp, commandIndex.front().timestamp, sizeof(stats.oldestTimestamp) - 1);
            stats.oldestTimestamp[sizeof(stats.oldestTimestamp) - 1] = '\0';
        }
        if (strlen(stats.newestTimestamp) == 0 ||
                parseTimestampToSeconds(commandIndex.back().timestamp) > parseTimestampToSeconds(stats.newestTimestamp)) {
                strncpy(stats.newestTimestamp, commandIndex.back().timestamp, sizeof(stats.newestTimestamp) - 1);
            stats.newestTimestamp[sizeof(stats.newestTimestamp) - 1] = '\0';
            }
        }
    }
    
    stats.uniqueUsers = uniqueUserCount;
    return stats;
}