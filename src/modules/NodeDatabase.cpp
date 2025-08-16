#include "NodeDatabase.h"
#include <SD.h>
#include "configuration.h"
#include "SimpleTimeManager.h"

extern SimpleTimeManager timeManager;

const char* NodeDatabase::NODE_DB_FILE = "/node_database.csv";

// Static instance for singleton-like behavior
static NodeDatabase* nodeDatabase = nullptr;

// Global accessor function
NodeDatabase* getNodeDatabase() {
    if (!nodeDatabase) {
        nodeDatabase = new NodeDatabase();
        LOG_INFO("Node database initialized");
    }
    return nodeDatabase;
}

NodeDatabase::NodeDatabase() {
    sdInitialized = true;
    dbLoaded = false;
}

bool NodeDatabase::initializeSD() {
    if (sdInitialized) {
        return true; // Already initialized
    }
    
    LOG_DEBUG("NodeDatabase: Initializing SD access...");
    
    if (!SD.begin()) {
        LOG_ERROR("NodeDatabase: SD card initialization failed");
        return false;
    }
    
    LOG_INFO("NodeDatabase: SD access initialized");
    sdInitialized = true;
    
    // Load the database now that SD is available
    loadDatabase();
    bootstrapFromMessageLog();
    
    return true;
}

const char* NodeDatabase::permissionToString(PermissionLevel level) {
    switch (level) {
        case PermissionLevel::UNKNOWN: return "UNKNOWN";
        case PermissionLevel::PUBLIC: return "PUBLIC";
        case PermissionLevel::MEMBER: return "MEMBER";
        case PermissionLevel::ADMIN: return "ADMIN";
        case PermissionLevel::OWNER: return "OWNER";
        default: return "UNKNOWN";
    }
}

PermissionLevel NodeDatabase::stringToPermission(const char* str) {
    if (strcmp(str, "PUBLIC") == 0) return PermissionLevel::PUBLIC;
    if (strcmp(str, "MEMBER") == 0) return PermissionLevel::MEMBER;
    if (strcmp(str, "ADMIN") == 0) return PermissionLevel::ADMIN;
    if (strcmp(str, "OWNER") == 0) return PermissionLevel::OWNER;
    return PermissionLevel::UNKNOWN;
}

void NodeDatabase::sanitizeForCSV(const char* input, char* output, size_t maxLen) {
    if (!input) {
        strncpy(output, "NULL", maxLen - 1);
        output[maxLen - 1] = '\0';
        return;
    }

    size_t inPos = 0, outPos = 0;
    while (input[inPos] && outPos < maxLen - 1) {
        char c = input[inPos];
        if (c == '"') {
            if (outPos < maxLen - 2) {
                output[outPos++] = '"';
                output[outPos++] = '"';
            }
        } else if (c == '\n' || c == '\r') {
            output[outPos++] = ' ';
        } else if (c >= 32 && c <= 126) {
            output[outPos++] = c;
        } else {
            output[outPos++] = '?';
        }
        inPos++;
    }
    output[outPos] = '\0';
}

bool NodeDatabase::parseCSVLine(const char* line, NodeRecord& record) {
    // Parse: nodeId,shortName,longName,permission,lastSeen,messageCount,isBlocked
    char lineCopy[512];
    memset(lineCopy, 0, sizeof(lineCopy));
    strncpy(lineCopy, line, sizeof(lineCopy) - 1);
    
    char* token = strtok(lineCopy, ",");
    if (!token) return false;
    record.nodeId = strtoul(token, nullptr, 16);
    
    token = strtok(nullptr, ",");
    if (!token) return false;
    strncpy(record.shortName, token, sizeof(record.shortName) - 1);
    record.shortName[sizeof(record.shortName) - 1] = '\0';
    
    token = strtok(nullptr, ",");
    if (!token) return false;
    strncpy(record.longName, token, sizeof(record.longName) - 1);
    record.longName[sizeof(record.longName) - 1] = '\0';
    
    token = strtok(nullptr, ",");
    if (!token) return false;
    record.permission = stringToPermission(token);
    
    token = strtok(nullptr, ",");
    if (!token) return false;
    strncpy(record.lastSeen, token, sizeof(record.lastSeen) - 1);
    record.lastSeen[sizeof(record.lastSeen) - 1] = '\0';
    
    token = strtok(nullptr, ",");
    if (!token) return false;
    record.messageCount = strtoul(token, nullptr, 10);
    
    token = strtok(nullptr, ",");
    if (!token) return false;
    record.isBlocked = (strcmp(token, "1") == 0 || strcmp(token, "true") == 0);
    
    return true;
}

bool NodeDatabase::loadDatabase() {
    if (!sdInitialized) {
        LOG_WARN("NodeDatabase: SD not initialized, cannot load database");
        return false;
    }
    
    LOG_INFO("Loading node database...");
    nodeRecords.clear();
    
    File file = SD.open(NODE_DB_FILE, FILE_READ);
    if (!file) {
        LOG_INFO("No existing node database found, creating new one");
        dbLoaded = true;
        return saveDatabase(); // Create the file with headers
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
            
            NodeRecord record;
            if (parseCSVLine(line, record)) {
                nodeRecords.push_back(record);
            }
        }
    }
    
    file.close();
    
    dbLoaded = true;
    LOG_INFO("Loaded %d node records", nodeRecords.size());
    return true;
}

bool NodeDatabase::saveDatabase() {
    if (!sdInitialized) {
        LOG_WARN("NodeDatabase: SD not initialized, cannot save database");
        return false;
    }
    
    LOG_INFO("Saving node database...");
    
    // Write to temp file first
    File tempFile = SD.open("/node_temp.csv", FILE_WRITE);
    if (!tempFile) {
        LOG_ERROR("Failed to create temp node database file");
        return false;
    }
    
    // Write header
    tempFile.println("nodeId,shortName,longName,permission,lastSeen,messageCount,isBlocked");
    
    // Write all records
    for (const auto& record : nodeRecords) {
        char safeShortName[64], safeLongName[128];
        sanitizeForCSV(record.shortName, safeShortName, sizeof(safeShortName));
        sanitizeForCSV(record.longName, safeLongName, sizeof(safeLongName));
        
        tempFile.printf("0x%08x,\"%s\",\"%s\",%s,\"%s\",%u,%s\n",
                       record.nodeId,
                       safeShortName,
                       safeLongName,
                       permissionToString(record.permission),
                       record.lastSeen,
                       record.messageCount,
                       record.isBlocked ? "1" : "0");
    }
    
    tempFile.close();
    
    // Replace the main file
    if (SD.exists(NODE_DB_FILE)) {
        SD.remove(NODE_DB_FILE);
    }
    
    bool success = false;
    if (SD.rename("/node_temp.csv", NODE_DB_FILE)) {
        LOG_INFO("Node database saved successfully");
        success = true;
    } else {
        LOG_ERROR("Failed to save node database");
        SD.remove("/node_temp.csv");
    }
    
    return success;
}

NodeRecord* NodeDatabase::findNode(uint32_t nodeId) {
    if (!dbLoaded) loadDatabase();
    
    for (auto& record : nodeRecords) {
        if (record.nodeId == nodeId) {
            return &record;
        }
    }
    return nullptr;
}

bool NodeDatabase::addOrUpdateNode(uint32_t nodeId, const char* shortName, const char* longName) {
    if (!dbLoaded) loadDatabase();
    
    NodeRecord* existing = findNode(nodeId);
    if (existing) {
        // Update existing node
        if (shortName && strlen(shortName) > 0) {
            strncpy(existing->shortName, shortName, sizeof(existing->shortName) - 1);
            existing->shortName[sizeof(existing->shortName) - 1] = '\0';
        }
        if (longName && strlen(longName) > 0) {
            strncpy(existing->longName, longName, sizeof(existing->longName) - 1);
            existing->longName[sizeof(existing->longName) - 1] = '\0';
        }
        return true;
    } else {
        // Add new node
        NodeRecord newRecord;
        newRecord.nodeId = nodeId;
        strncpy(newRecord.shortName, shortName ? shortName : "Unknown", sizeof(newRecord.shortName) - 1);
        newRecord.shortName[sizeof(newRecord.shortName) - 1] = '\0';
        strncpy(newRecord.longName, longName ? longName : "Unknown Node", sizeof(newRecord.longName) - 1);
        newRecord.longName[sizeof(newRecord.longName) - 1] = '\0';
        newRecord.permission = defaultPermission;
        strncpy(newRecord.lastSeen, getTimeManager()->getTimeString().c_str(), sizeof(newRecord.lastSeen) - 1);
        newRecord.lastSeen[sizeof(newRecord.lastSeen) - 1] = '\0';
        newRecord.messageCount = 0;
        newRecord.isBlocked = false;
        
        nodeRecords.push_back(newRecord);
        LOG_INFO("Added new node: 0x%08x (%s) with %s permission", 
                nodeId, newRecord.shortName, permissionToString(defaultPermission));
        return true;
    }
}

bool NodeDatabase::updateLastSeen(uint32_t nodeId, const char* timestamp) {
    NodeRecord* node = findNode(nodeId);
    if (node) {
        strncpy(node->lastSeen, timestamp, sizeof(node->lastSeen) - 1);
        node->lastSeen[sizeof(node->lastSeen) - 1] = '\0';
        return true;
    }
    return false;
}

void NodeDatabase::incrementMessageCount(uint32_t nodeId) {
    NodeRecord* node = findNode(nodeId);
    if (node) {
        node->messageCount++;
    }
}

PermissionLevel NodeDatabase::getPermissionLevel(uint32_t nodeId) {
    NodeRecord* node = findNode(nodeId);
    return node ? node->permission : PermissionLevel::UNKNOWN;
}

bool NodeDatabase::setPermissionLevel(uint32_t nodeId, PermissionLevel level) {
    NodeRecord* node = findNode(nodeId);
    if (node) {
        node->permission = level;
        LOG_INFO("Set permission for 0x%08x to %s", nodeId, permissionToString(level));
        return true;
    }
    return false;
}

bool NodeDatabase::isBlocked(uint32_t nodeId) {
    NodeRecord* node = findNode(nodeId);
    return node ? node->isBlocked : false;
}

bool NodeDatabase::setBlocked(uint32_t nodeId, bool blocked) {
    NodeRecord* node = findNode(nodeId);
    if (node) {
        node->isBlocked = blocked;
        LOG_INFO("%s node 0x%08x", blocked ? "Blocked" : "Unblocked", nodeId);
        return true;
    }
    return false;
}

bool NodeDatabase::hasPermission(uint32_t nodeId, PermissionLevel requiredLevel) {
    if (isBlocked(nodeId)) return false;
    
    PermissionLevel userLevel = getPermissionLevel(nodeId);
    return static_cast<int>(userLevel) >= static_cast<int>(requiredLevel);
}

bool NodeDatabase::canExecuteCommand(uint32_t nodeId, const char* command) {
    if (isBlocked(nodeId)) return false;
    
    PermissionLevel userLevel = getPermissionLevel(nodeId);
    
    // Define command permissions
    if (strcmp(command, "/getTime") == 0 || strcmp(command, "/battery") == 0) {
        return static_cast<int>(userLevel) >= static_cast<int>(PermissionLevel::PUBLIC);
    }
    if (strncmp(command, "/logs", 5) == 0) {
        return static_cast<int>(userLevel) >= static_cast<int>(PermissionLevel::MEMBER);
    }
    if (strncmp(command, "/setTime", 8) == 0 || strncmp(command, "/admin", 6) == 0) {
        return static_cast<int>(userLevel) >= static_cast<int>(PermissionLevel::ADMIN);
    }
    if (strncmp(command, "/user", 5) == 0) {
        return static_cast<int>(userLevel) >= static_cast<int>(PermissionLevel::OWNER);
    }
    
    // Default to PUBLIC level for unknown commands
    return static_cast<int>(userLevel) >= static_cast<int>(PermissionLevel::PUBLIC);
}

std::vector<NodeRecord> NodeDatabase::getAllNodes() {
    if (!dbLoaded) loadDatabase();
    return nodeRecords;
}

std::vector<NodeRecord> NodeDatabase::getNodesByPermission(PermissionLevel level) {
    if (!dbLoaded) loadDatabase();
    
    std::vector<NodeRecord> result;
    for (const auto& record : nodeRecords) {
        if (record.permission == level) {
            result.push_back(record);
        }
    }
    return result;
}

uint32_t NodeDatabase::getNodeCount() {
    if (!dbLoaded) loadDatabase();
    return nodeRecords.size();
}

NodeDatabase::NodeStats NodeDatabase::getStats() {
    if (!dbLoaded) loadDatabase();
    
    NodeStats stats = {};
    
    for (const auto& record : nodeRecords) {
        stats.totalNodes++;
        
        switch (record.permission) {
            case PermissionLevel::UNKNOWN: stats.unknownNodes++; break;
            case PermissionLevel::PUBLIC: stats.publicNodes++; break;
            case PermissionLevel::MEMBER: stats.memberNodes++; break;
            case PermissionLevel::ADMIN: stats.adminNodes++; break;
            case PermissionLevel::OWNER: stats.ownerNodes++; break;
        }
        
        if (record.isBlocked) stats.blockedNodes++;
    }
    
    return stats;
}

bool NodeDatabase::bootstrapFromMessageLog() {
    if (!sdInitialized) {
        LOG_WARN("NodeDatabase: SD not initialized, cannot bootstrap from message log");
        return false;
    }
    
    LOG_INFO("Bootstrapping node database from existing message log...");
    
    File file = SD.open("/message_log.csv", FILE_READ);
    if (!file) {
        LOG_INFO("No existing message log found");
        return false;
    }
    
    char line[512];
    bool firstLine = true;
    int addedUsers = 0;
    
    while (file.available()) {
        memset(line, 0, sizeof(line));
        int lineLength = file.readBytesUntil('\n', line, sizeof(line) - 1);
        
        if (lineLength > 0) {
            line[lineLength] = '\0';
            
            if (firstLine) {
                firstLine = false;
                continue; // Skip header
            }
            
            // Parse: timestamp,from,to,sender_name,channel,message
            char* timestamp = strtok(line, ",");
            char* fromStr = strtok(nullptr, ",");
            char* toStr = strtok(nullptr, ",");
            char* senderName = strtok(nullptr, ",");
            
            if (fromStr && senderName) {
                uint32_t fromId = strtoul(fromStr, nullptr, 16);
                
                // Remove quotes from sender name if present
                if (senderName[0] == '"') {
                    senderName++;
                    char* endQuote = strchr(senderName, '"');
                    if (endQuote) *endQuote = '\0';
                }
                
                // Add user if not already known
                if (!findNode(fromId)) {
                    addOrUpdateNode(fromId, senderName, nullptr);
                    addedUsers++;
                }
            }
        }
    }
    
    file.close();
    
    LOG_INFO("Bootstrap complete: added %d users from message log", addedUsers);
    return saveDatabase();
}