#pragma once

#include <Arduino.h>
#include <vector>

enum class PermissionLevel {
    UNKNOWN = 0,    // New/unrecognized nodes
    PUBLIC = 1,     // Basic access - time, battery status
    MEMBER = 2,     // Log queries, search functions  
    ADMIN = 3,      // Full system access, user management
    OWNER = 4       // Complete control, can modify permissions
};

struct NodeRecord {
    uint32_t nodeId;
    char shortName[32];
    char longName[64];
    PermissionLevel permission;
    char lastSeen[32];       // Timestamp of last message
    uint32_t messageCount;   // Total messages from this node
    bool isBlocked;          // If true, ignore all commands from this node
};

class NodeDatabase {
private:
    static const char* NODE_DB_FILE;
    std::vector<NodeRecord> nodeRecords;
    bool dbLoaded = false;
    bool sdInitialized = true;
    
    // Helper functions
    bool parseCSVLine(const char* line, NodeRecord& record);
    void sanitizeForCSV(const char* input, char* output, size_t maxLen);
    
public:
    NodeDatabase();
    
    // Initialize SD access (should be called once at startup)
    bool initializeSD();
    
    bool bootstrapFromMessageLog();
    
    PermissionLevel stringToPermission(const char* str);
    const char* permissionToString(PermissionLevel level);
    
    // Core functions
    bool loadDatabase();
    bool saveDatabase();
    
    // Node management
    NodeRecord* findNode(uint32_t nodeId);
    bool addOrUpdateNode(uint32_t nodeId, const char* shortName, const char* longName);
    bool updateLastSeen(uint32_t nodeId, const char* timestamp);
    void incrementMessageCount(uint32_t nodeId);
    
    // Permission management
    PermissionLevel getPermissionLevel(uint32_t nodeId);
    bool setPermissionLevel(uint32_t nodeId, PermissionLevel level);
    bool isBlocked(uint32_t nodeId);
    bool setBlocked(uint32_t nodeId, bool blocked);
    
    // Permission checks
    bool hasPermission(uint32_t nodeId, PermissionLevel requiredLevel);
    bool canExecuteCommand(uint32_t nodeId, const char* command);
    
    // Administrative functions
    std::vector<NodeRecord> getAllNodes();
    std::vector<NodeRecord> getNodesByPermission(PermissionLevel level);
    uint32_t getNodeCount();
    
    // Statistics
    struct NodeStats {
        uint32_t totalNodes;
        uint32_t unknownNodes;
        uint32_t publicNodes;
        uint32_t memberNodes;
        uint32_t adminNodes;
        uint32_t ownerNodes;
        uint32_t blockedNodes;
    };
    NodeStats getStats();
    
    // Default permissions for new nodes
    void setDefaultPermission(PermissionLevel level) { defaultPermission = level; }
    PermissionLevel getDefaultPermission() { return defaultPermission; }
    
    // Status check
    bool isInitialized() { return sdInitialized; }
    
private:
    PermissionLevel defaultPermission = PermissionLevel::PUBLIC;
};

// Get the global node database instance
NodeDatabase* getNodeDatabase();