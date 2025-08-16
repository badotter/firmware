#pragma once

#include <Arduino.h>
#include <vector>
#include "FSCommon.h"

struct MessageRecord {
    char timestamp[32];
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
    char startTime[32] = "";
    char endTime[32] = "";
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
    static const char* COMMAND_LOG_FILE;
    
    std::vector<MessageRecord> allMessages;
    std::vector<MessageRecord> allCommands;
    bool messagesLoaded = false;
    bool commandsLoaded = false;
    
    // Helper functions
    bool parseCSVLine(const char* line, MessageRecord& record);
    bool matchesFilter(const MessageRecord& record, const QueryFilter& filter);
    bool isTimestampInRange(const char* timestamp, const char* startTime, const char* endTime) const;
    
public:
    LogQuerySystem();
    
    // Helper functions for timestamp handling (made public static)
    static uint32_t parseTimestampToSeconds(const char* timestamp);
    
    // Load data from SD card into RAM
    bool loadMessages();
    bool loadCommands();
    void refreshData();  // Reload both if needed
    
    // Main query functions
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
    
    // Statistics
    struct LogStats {
        uint32_t totalMessages;
        uint32_t totalDMs;
        uint32_t totalBroadcasts;
        uint32_t totalCommands;
        uint32_t uniqueUsers;
        char oldestTimestamp[32];
        char newestTimestamp[32];
    };
    LogStats getLogStatistics();
    
    // Status
    int getMessageCount() { return allMessages.size(); }
    int getCommandCount() { return allCommands.size(); }
};