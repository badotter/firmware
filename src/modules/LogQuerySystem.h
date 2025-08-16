#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include "FSCommon.h"

struct MessageRecord {
    char timestamp[32];  // Changed from uint32_t to string
    uint32_t from;
    uint32_t to;
    char senderName[32];
    uint32_t channelHash;
    char message[256];
    bool isDM;  // true if to != 0
    
    // Helper function to convert timestamp string to seconds for comparisons
    uint32_t getTimestampSeconds() const;
};

struct QueryFilter {
    char startTime[32] = "";      // Changed from uint32_t to string
    char endTime[32] = "";        // Changed from uint32_t to string  
    uint32_t fromUser = 0;        // 0 = any user
    uint32_t toUser = 0;          // 0 = any recipient
    uint32_t channelHash = 0;     // 0 = base channel
    bool onlyDMs = false;
    bool onlyBroadcast = false;
    char searchText[64] = "";     // Empty = no text search
    int maxResults = 50;
};

class LogQuerySystem {
private:
    static const char* MESSAGE_LOG_FILE;
    static const char* INDEX_FILE;
    static const char* COMMAND_LOG_FILE;
    
    struct IndexEntry {
        char timestamp[32];       // Changed from uint32_t to string
        uint32_t filePosition;
        uint32_t from;
        uint32_t channelHash;
        bool isDM;
        
        // Helper function for timestamp comparisons
        uint32_t getTimestampSeconds() const;
    };
    
    std::vector<IndexEntry> messageIndex;
    std::vector<IndexEntry> commandIndex;
    bool indexLoaded = false;
    bool commandIndexLoaded = false;
    
public:
    // Helper functions for timestamp handling (made public static)
    static uint32_t parseTimestampToSeconds(const char* timestamp);
    bool isTimestampInRange(const char* timestamp, const char* startTime, const char* endTime) const;

private:
    
public:
    LogQuerySystem();
    
    // Main query function
    std::vector<MessageRecord> queryMessages(const QueryFilter& filter);
    std::vector<MessageRecord> queryCommands(const QueryFilter& filter);
    
    // Convenience functions for common queries
    std::vector<MessageRecord> getRecentMessages(int count = 10);
    std::vector<MessageRecord> getRecentCommands(int count = 10);
    std::vector<MessageRecord> getMessagesFromUser(uint32_t fromUser, int count = 20);
    std::vector<MessageRecord> getCommandsFromUser(uint32_t fromUser, int count = 20);
    std::vector<MessageRecord> getDMsWithUser(uint32_t user, int count = 20);
    std::vector<MessageRecord> getChannelMessages(uint32_t channelHash, int count = 20);
    std::vector<MessageRecord> getTodaysMessages();
    std::vector<MessageRecord> getTodaysCommands();
    std::vector<MessageRecord> searchMessages(const char* searchText, int count = 20);
    std::vector<MessageRecord> searchCommands(const char* searchText, int count = 20);
    std::vector<MessageRecord> readRecentRecords(const char* filename, int maxCount);

    // Index management
    bool rebuildIndex();
    bool rebuildCommandIndex();
    bool loadIndex();
    bool loadCommandIndex();
    void addToIndex(const MessageRecord& record, uint32_t filePosition);
    
    // Statistics
    struct LogStats {
        uint32_t totalMessages;
        uint32_t totalDMs;
        uint32_t totalBroadcasts;
        uint32_t totalCommands;
        uint32_t uniqueUsers;
        char oldestTimestamp[32];   // Changed from uint32_t to string
        char newestTimestamp[32];   // Changed from uint32_t to string
    };
    LogStats getLogStatistics();
    
private:
    bool parseCSVLine(const char* line, MessageRecord& record);
    bool matchesFilter(const MessageRecord& record, const QueryFilter& filter);
    std::vector<MessageRecord> readRecordsAtPositions(const std::vector<uint32_t>& positions);
    std::vector<MessageRecord> readCommandsAtPositions(const std::vector<uint32_t>& positions);
};