#include "TextMessageModule.h"
#include "Router.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "PowerStatus.h"

#ifdef HAS_SDCARD
#include "Logger.h"
#include "LogQuerySystem.h"
#include "SimpleTimeManager.h"
#include "NodeDatabase.h"

// Initialize global instances
LogQuerySystem* logQuery = nullptr;

// Lazy initialization functions
LogQuerySystem* getLogQuery() {
    if (!logQuery) {  
        logQuery = new LogQuerySystem();
        LOG_INFO("Log query system initialized");
    }
    return logQuery;
}
#endif

TextMessageModule *textMessageModule;
extern meshtastic::PowerStatus *powerStatus;

// Forward declarations for command handlers
void handleCommand(uint32_t fromNode, const char* commandText, uint32_t toNode);
void handleGetTimeCommand(uint32_t fromNode, char** parts, int partCount);
void handleSetTimeCommand(uint32_t fromNode, char** parts, int partCount);
void handleBatteryCommand(uint32_t fromNode, char** parts, int partCount);
void handleLogsCommand(uint32_t fromNode, char** parts, int partCount);
void handleUsersCommand(uint32_t fromNode, char** parts, int partCount);

void sendCommandResponse(uint32_t toNode, const char* message) {
    meshtastic_MeshPacket *p = router->allocForSending();

    p->to = toNode;
    p->from = nodeDB->getNodeNum();
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;

    size_t len = strlen(message);
    if (len > sizeof(p->decoded.payload.bytes)) {
        len = sizeof(p->decoded.payload.bytes);
    }

    memcpy(p->decoded.payload.bytes, message, len);
    p->decoded.payload.size = len;

    // Send it
    service->sendToMesh(p, RxSource::RX_SRC_LOCAL);
}

ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef DEBUG_PORT
    auto &p = mp.decoded;
    LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif

#ifdef HAS_SDCARD
    // Get sender info
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(mp.from);
    const char* senderName = "Unknown";
    const char* longName = "Unknown Node";
    if (node && node->has_user) {
        senderName = node->user.short_name;
        longName = node->user.long_name;
    }

    // Update node database
    NodeDatabase* ndb = getNodeDatabase();
    ndb->addOrUpdateNode(mp.from, senderName, longName);
    ndb->updateLastSeen(mp.from, getTimeManager()->getTimeString().c_str());
    ndb->incrementMessageCount(mp.from);

    // Extract the message text
    char messageText[256];
    size_t msgLen = mp.decoded.payload.size;
    if (msgLen > sizeof(messageText) - 1) {
        msgLen = sizeof(messageText) - 1;
    }
    memcpy(messageText, mp.decoded.payload.bytes, msgLen);
    messageText[msgLen] = '\0';

    // Handle commands
    if (messageText[0] == '/') {
        LOG_DEBUG("Processing command from 0x%08x: %s", mp.from, messageText);
        
        // Check if user is blocked
        if (ndb->isBlocked(mp.from)) {
            LOG_INFO("Ignoring command from blocked user 0x%08x", mp.from);
            // Still log blocked commands for audit purposes
            bool logResult = meshLogger.addCommandLog(mp.from, mp.to, senderName, mp.channel, messageText);
            LOG_DEBUG("Command log result (blocked user): %s", logResult ? "success" : "failed");
            return ProcessMessage::CONTINUE;
        }
        
        // Check basic permission for any command
        if (!ndb->hasPermission(mp.from, PermissionLevel::PUBLIC)) {
            sendCommandResponse(mp.from, "Access denied. Unknown user.");
            // Still log denied commands for audit purposes
            bool logResult = meshLogger.addCommandLog(mp.from, mp.to, senderName, mp.channel, messageText);
            LOG_DEBUG("Command log result (denied): %s", logResult ? "success" : "failed");
            return ProcessMessage::CONTINUE;
        }
        
        // Log command BEFORE processing it to ensure it gets logged even if processing fails
        bool logResult = meshLogger.addCommandLog(mp.from, mp.to, senderName, mp.channel, messageText);
        LOG_DEBUG("Command log result: %s", logResult ? "success" : "failed");
        
        // Now process the command
        handleCommand(mp.from, messageText, mp.to);
        
        return ProcessMessage::CONTINUE;
    }

    // Log regular messages
    bool logResult = meshLogger.addMessageLog(mp.from, mp.to, senderName, mp.channel, messageText);
    LOG_DEBUG("Message log result: %s", logResult ? "success" : "failed");
#endif

    // We only store/display messages destined for us.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;

    powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE;
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}

void parseCommand(const char* commandText, char** parts, int& partCount, int maxParts) {
    partCount = 0;
    char* buffer = strdup(commandText);
    char* token = strtok(buffer, " ");
    LOG_DEBUG("Parsing command: %s", commandText);

    while (token != nullptr && partCount < maxParts) {
        parts[partCount] = strdup(token);
        LOG_DEBUG("argument: %s", token);
        partCount++;
        token = strtok(nullptr, " ");
    }

    free(buffer);
}

void handleCommand(uint32_t fromNode, const char* commandText, uint32_t toNode) {
    char* parts[10];
    int partCount = 0;
    parseCommand(commandText, parts, partCount, 10);

    if (partCount == 0) {
        // Clean up and return
        for (int i = 0; i < partCount; i++) {
            free(parts[i]);
        }
        return;
    }

    char *cmd = parts[0];
    LOG_DEBUG("Command %s args %d from 0x%08x", cmd, partCount, fromNode);

    // Route to appropriate command handler
    if (strcmp(cmd, "/getTime") == 0) {
        handleGetTimeCommand(fromNode, parts, partCount);
    } else if (strcmp(cmd, "/setTime") == 0) {
        handleSetTimeCommand(fromNode, parts, partCount);
    } else if (strcmp(cmd, "/battery") == 0) {
        handleBatteryCommand(fromNode, parts, partCount);
    } else if (strcmp(cmd, "/logs") == 0) {
        handleLogsCommand(fromNode, parts, partCount);
    } else if (strcmp(cmd, "/users") == 0) {
        handleUsersCommand(fromNode, parts, partCount);
    } else {
        char responseMsg[256];
        memset(responseMsg, 0, sizeof(responseMsg));
        snprintf(responseMsg, sizeof(responseMsg), "Unknown command: %s", cmd);
        sendCommandResponse(fromNode, responseMsg);
    }

    // Clean up allocated strings
    for (int i = 0; i < partCount; i++) {
        free(parts[i]);
    }
}

void handleGetTimeCommand(uint32_t fromNode, char** parts, int partCount) {
    String timeStr = getTimeManager()->getTimeString();
    char responseMsg[256];
    memset(responseMsg, 0, sizeof(responseMsg));
    snprintf(responseMsg, sizeof(responseMsg), "Current time is: %s", timeStr.c_str());
    sendCommandResponse(fromNode, responseMsg);
}

void handleSetTimeCommand(uint32_t fromNode, char** parts, int partCount) {
    char responseMsg[256];
    memset(responseMsg, 0, sizeof(responseMsg));
    
    if (partCount < 3) {
        snprintf(responseMsg, sizeof(responseMsg), "Usage: /setTime mm/dd/yyyy hh:mm:ss");
        sendCommandResponse(fromNode, responseMsg);
        return;
    }

    char timeString[32];
    snprintf(timeString, sizeof(timeString), "%s %s", parts[1], parts[2]);
    timeString[sizeof(timeString) - 1] = '\0';
    
    if (getTimeManager()->setTime(timeString)) {
        snprintf(responseMsg, sizeof(responseMsg), "Time set to: %s", timeString);
    } else {
        snprintf(responseMsg, sizeof(responseMsg), "Failed to set time. Use format: mm/dd/yyyy hh:mm:ss");
    }
    sendCommandResponse(fromNode, responseMsg);
}

void handleBatteryCommand(uint32_t fromNode, char** parts, int partCount) {
    int batteryLevel = powerStatus->getBatteryVoltageMv();
    char responseMsg[256];
    memset(responseMsg, 0, sizeof(responseMsg));
    snprintf(responseMsg, sizeof(responseMsg), "Battery voltage: %dmV", batteryLevel);
    sendCommandResponse(fromNode, responseMsg);
}

void handleLogsCommand(uint32_t fromNode, char** parts, int partCount) {
    char responseMsg[256];
    memset(responseMsg, 0, sizeof(responseMsg));
    
    if (partCount < 2) {
        sendCommandResponse(fromNode, "Usage: /logs [recent|from|search|stats|commands] [args]");
        return;
    }

    if (strcmp(parts[1], "recent") == 0) {
        int count = (partCount > 2) ? atoi(parts[2]) : 5;
        std::vector<MessageRecord> result = getLogQuery()->getRecentMessages(min(count, 10));
        
        snprintf(responseMsg, sizeof(responseMsg), "Recent %d messages:", result.size());
        sendCommandResponse(fromNode, responseMsg);
        
        for (const auto& msg : result) {
            snprintf(responseMsg, sizeof(responseMsg), "%s: %s", msg.senderName, msg.message);
            sendCommandResponse(fromNode, responseMsg);
            delay(200);
        }
        
    } else if (strcmp(parts[1], "from") == 0 && partCount > 2) {
        uint32_t targetUser = strtoul(parts[2], nullptr, 16);
        std::vector<MessageRecord> result = getLogQuery()->getMessagesFromUser(targetUser, 5);
        
        snprintf(responseMsg, sizeof(responseMsg), "%d Messages from 0x%08x:", result.size(), targetUser);
        sendCommandResponse(fromNode, responseMsg);
        
        for (const auto& msg : result) {
            snprintf(responseMsg, sizeof(responseMsg), "%s", msg.message);
            sendCommandResponse(fromNode, responseMsg);
            delay(200);
        }
        
    } else if (strcmp(parts[1], "search") == 0 && partCount > 2) {
        std::vector<MessageRecord> result = getLogQuery()->searchMessages(parts[2], 5);
        snprintf(responseMsg, sizeof(responseMsg), "Found %d messages with '%s':", result.size(), parts[2]);
        sendCommandResponse(fromNode, responseMsg);
        
        for (const auto& msg : result) {
            snprintf(responseMsg, sizeof(responseMsg), "%s: %s", msg.senderName, msg.message);
            sendCommandResponse(fromNode, responseMsg);
            delay(200);
        }
        
    } else if (strcmp(parts[1], "stats") == 0) {
        LogQuerySystem::LogStats stats = getLogQuery()->getLogStatistics();
        snprintf(responseMsg, sizeof(responseMsg), 
                "Stats: %d msgs, %d cmds, %d DMs, %d users", 
                stats.totalMessages, stats.totalCommands, stats.totalDMs, stats.uniqueUsers);
        sendCommandResponse(fromNode, responseMsg);

    } else if (strcmp(parts[1], "commands") == 0) {
        if (partCount > 2 && strcmp(parts[2], "recent") == 0) {
            int count = (partCount > 3) ? atoi(parts[3]) : 5;
            std::vector<MessageRecord> result = getLogQuery()->getRecentCommands(min(count, 10));
            
            snprintf(responseMsg, sizeof(responseMsg), "Recent %d commands:", result.size());
            sendCommandResponse(fromNode, responseMsg);
            
            for (const auto& cmd : result) {
                snprintf(responseMsg, sizeof(responseMsg), "%s: %s", cmd.senderName, cmd.message);
                sendCommandResponse(fromNode, responseMsg);
                delay(200);
            }
        } else if (partCount > 3 && strcmp(parts[2], "from") == 0) {
            uint32_t targetUser = strtoul(parts[3], nullptr, 16);
            std::vector<MessageRecord> result = getLogQuery()->getCommandsFromUser(targetUser, 5);
            
            snprintf(responseMsg, sizeof(responseMsg), "%d Commands from 0x%08x:", result.size(), targetUser);
            sendCommandResponse(fromNode, responseMsg);
            
            for (const auto& cmd : result) {
                snprintf(responseMsg, sizeof(responseMsg), "%s", cmd.message);
                sendCommandResponse(fromNode, responseMsg);
                delay(200);
            }
        } else if (partCount > 3 && strcmp(parts[2], "search") == 0) {
            std::vector<MessageRecord> result = getLogQuery()->searchCommands(parts[3], 5);
            snprintf(responseMsg, sizeof(responseMsg), "Found %d commands with '%s':", result.size(), parts[3]);
            sendCommandResponse(fromNode, responseMsg);
            
            for (const auto& cmd : result) {
                snprintf(responseMsg, sizeof(responseMsg), "%s: %s", cmd.senderName, cmd.message);
                sendCommandResponse(fromNode, responseMsg);
                delay(200);
            }
        } else {
            sendCommandResponse(fromNode, "Usage: /logs commands [recent|from|search] [args]");
        }
        
    } else {
        snprintf(responseMsg, sizeof(responseMsg), "Unknown logs command. Try: recent, from, search, stats, commands");
        sendCommandResponse(fromNode, responseMsg);
    }
}

void handleUsersCommand(uint32_t fromNode, char** parts, int partCount) {
    char responseMsg[256];
    memset(responseMsg, 0, sizeof(responseMsg));
    NodeDatabase* ndb = getNodeDatabase();

    if (partCount < 2) {
        auto stats = ndb->getStats();
        snprintf(responseMsg, sizeof(responseMsg), 
                "Users: %d total (%d public, %d member, %d admin, %d owner, %d blocked)",
                stats.totalNodes, stats.publicNodes, stats.memberNodes, 
                stats.adminNodes, stats.ownerNodes, stats.blockedNodes);
        sendCommandResponse(fromNode, responseMsg);
        
    } else if (strcmp(parts[1], "list") == 0) {
        auto nodes = ndb->getAllNodes();
        sendCommandResponse(fromNode, "Known users:");
        
        for (const auto& node : nodes) {
            snprintf(responseMsg, sizeof(responseMsg), "0x%08x %s (%s) - %s%s", 
                    node.nodeId, node.shortName, 
                    ndb->permissionToString(node.permission),
                    node.isBlocked ? " [BLOCKED]" : "",
                    node.messageCount > 0 ? "" : " [NEW]");
            sendCommandResponse(fromNode, responseMsg);
            delay(100);
        }
        
    } else if (strcmp(parts[1], "promote") == 0 && partCount > 2) {
        uint32_t targetId = strtoul(parts[2], nullptr, 16);
        PermissionLevel currentLevel = ndb->getPermissionLevel(targetId);
        PermissionLevel newLevel = static_cast<PermissionLevel>(static_cast<int>(currentLevel) + 1);
        
        if (newLevel <= PermissionLevel::OWNER && ndb->setPermissionLevel(targetId, newLevel)) {
            snprintf(responseMsg, sizeof(responseMsg), "Promoted 0x%08x to %s", 
                    targetId, ndb->permissionToString(newLevel));
            ndb->saveDatabase();
        } else {
            snprintf(responseMsg, sizeof(responseMsg), "Failed to promote 0x%08x", targetId);
        }
        sendCommandResponse(fromNode, responseMsg);
        
    } else if (strcmp(parts[1], "demote") == 0 && partCount > 2) {
        uint32_t targetId = strtoul(parts[2], nullptr, 16);
        PermissionLevel currentLevel = ndb->getPermissionLevel(targetId);
        PermissionLevel newLevel = static_cast<PermissionLevel>(static_cast<int>(currentLevel) - 1);
        
        if (newLevel >= PermissionLevel::UNKNOWN && ndb->setPermissionLevel(targetId, newLevel)) {
            snprintf(responseMsg, sizeof(responseMsg), "Demoted 0x%08x to %s", 
                    targetId, ndb->permissionToString(newLevel));
            ndb->saveDatabase();
        } else {
            snprintf(responseMsg, sizeof(responseMsg), "Failed to demote 0x%08x", targetId);
        }
        sendCommandResponse(fromNode, responseMsg);
        
    } else if (strcmp(parts[1], "block") == 0 && partCount > 2) {
        uint32_t targetId = strtoul(parts[2], nullptr, 16);
        if (ndb->setBlocked(targetId, true)) {
            snprintf(responseMsg, sizeof(responseMsg), "Blocked user 0x%08x", targetId);
            ndb->saveDatabase();
        } else {
            snprintf(responseMsg, sizeof(responseMsg), "Failed to block 0x%08x", targetId);
        }
        sendCommandResponse(fromNode, responseMsg);
        
    } else if (strcmp(parts[1], "unblock") == 0 && partCount > 2) {
        uint32_t targetId = strtoul(parts[2], nullptr, 16);
        if (ndb->setBlocked(targetId, false)) {
            snprintf(responseMsg, sizeof(responseMsg), "Unblocked user 0x%08x", targetId);
            ndb->saveDatabase();
        } else {
            snprintf(responseMsg, sizeof(responseMsg), "Failed to unblock 0x%08x", targetId);
        }
        sendCommandResponse(fromNode, responseMsg);
        
    } else {
        snprintf(responseMsg, sizeof(responseMsg), "Unknown users command. Try: list, promote, demote, block, unblock");
        sendCommandResponse(fromNode, responseMsg);
    }
}