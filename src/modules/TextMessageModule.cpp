#include "TextMessageModule.h"
#include "Router.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"

#ifdef HAS_SDCARD
#include "Logger.h"
#endif

TextMessageModule *textMessageModule;


void handleCommand(uint32_t fromNode, const char* commandText, uint32_t toNode);
void handleLogsCommand(uint32_t fromNode, const std::vector<std::string>& parts, uint32_t toNode);
void sendResponse(uint32_t toNode, const char* message);

ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef DEBUG_PORT
    auto &p = mp.decoded;
    LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif
#ifdef HAS_SDCARD
    // Log the received message to SD card
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(mp.from);
    const char* senderName = "Unknown";
    if (node && node->has_user) {
        senderName = node->user.short_name;
    }

    // Extract the message text
    char messageText[256];
    size_t msgLen = mp.decoded.payload.size;
    if (msgLen > sizeof(messageText) - 1) {
        msgLen = sizeof(messageText) - 1;
    }
    memcpy(messageText, mp.decoded.payload.bytes, msgLen);
    messageText[msgLen] = '\0';

    if (messageText[0] == '/') {
        // This is a command, handle it
        handleCommand(mp.from, messageText, mp.to);
        return ProcessMessage::CONTINUE; // Still log it, but also process as command
    }

    // Log it (using channel 0 for now, you could determine actual channel if needed)
    bufferedLogger.addMessageLog(mp.from, mp.to, senderName, 0, messageText);
#endif

    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;

    powerFSM.trigger(EVENT_RECEIVED_MSG);
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}


void handleLogsCommand(uint32_t fromNode, const std::vector<std::string>& parts, uint32_t toNode) {
    // parts[0] = "/logs", parts[1] = subcommand, parts[2+] = arguments
    /*
    if (parts.size() < 2) {
        sendResponse(fromNode, "Usage: /logs [recent|count|from|today] [args]");
        return;
    }

    std::string subcommand = parts[1];
    if (subcommand == "recent") {
        int count = (parts.size() > 2) ? atoi(parts[2].c_str()) : 5;
        count = min(count, 20); // Limit to prevent spam
        handleRecentLogs(fromNode, count);
    } else if (subcommand == "count") {
        handleLogCount(fromNode);
    } else if (subcommand == "from") {
        if (parts.size() < 3) {
            sendResponse(fromNode, "Usage: /logs from 0x12345678");
            return;
        }
        uint32_t targetNode = strtoul(parts[2].c_str(), nullptr, 16);
        handleLogsFromNode(fromNode, targetNode);
    } else if (subcommand == "today") {
        handleTodayLogs(fromNode);
    } else {
        sendResponse(fromNode, "Unknown logs command. Try: recent, count, from, today");
    }*/
}

void parseCommand(const char* commandText, char** parts, int& partCount, int maxParts) {
    partCount = 0;
    char* buffer = strdup(commandText);  // Make a copy we can modify
    char* token = strtok(buffer, " ");
    LOG_DEBUG("Parsing command: %s", commandText);

    while (token != nullptr && partCount < maxParts) {
        parts[partCount] = strdup(token);  // Copy each part
        LOG_DEBUG("argument:  %s", token);
        partCount++;
        token = strtok(nullptr, " ");
    }

    free(buffer);
}

void handleCommand(uint32_t fromNode, const char* commandText, uint32_t toNode) {
    char* parts[10];  // Max 10 command parts
    int partCount = 0;

    parseCommand(commandText, parts, partCount, 10);

    char *cmd = parts[0];
    LOG_DEBUG("Command  %s  args %d", cmd, partCount);
    if (partCount > 1) {
        // Handle the first argument if it exists
        LOG_DEBUG("Arg  %s", parts[1]);
    }
    char responseMsg[256];
    snprintf(responseMsg, sizeof(responseMsg), "Got a command: %s", commandText);
    sendResponse(fromNode, responseMsg);
    //if (cmd == "logs") {
    //    handleLogsCommand(fromNode, parts, toNode);
    //}// else if (cmd == "status") {
    //    handleStatusCommand(fromNode, toNode);
    //}// else if (cmd == "time") {
    //    handleTimeCommand(fromNode, toNode);
    //}
}

void sendResponse(uint32_t toNode, const char* message) {
    meshtastic_MeshPacket *p = router->allocForSending(); // Still need to find this function

    p->to = toNode;        // ✅ DM to specific node (not 0)
    p->from = nodeDB->getNodeNum();  // Your node ID
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

///////////////////////////////////////////////////////////////////////////
// Example usage of LogQuerySystem in TextMessageModule

/*
#ifdef HAS_SDCARD
#include "LogQuerySystem.h"
LogQuerySystem* logQuery = nullptr;
#endif

// In TextMessageModule constructor or init:
#ifdef HAS_SDCARD
    if (!logQuery) {
        logQuery = new LogQuerySystem();
    }
#endif

// Update your handleCommand function:
void handleCommand(uint32_t fromNode, const char* commandText, uint32_t toNode) {
    char* parts[10];
    int partCount = 0;
    parseCommand(commandText, parts, partCount, 10);

    if (partCount == 0) return;

    char responseMsg[256];

    if (strcmp(parts[0], "/logs") == 0) {
        if (partCount < 2) {
            sendResponse(fromNode, "Usage: /logs [recent|from|dms|channel|search|stats] [args]");
            return;
        }

        if (strcmp(parts[1], "recent") == 0) {
            int count = (partCount > 2) ? atoi(parts[2]) : 5;
            auto messages = logQuery->getRecentMessages(min(count, 10));

            for (const auto& msg : messages) {
                snprintf(responseMsg, sizeof(responseMsg), "%s: %s", msg.senderName, msg.message);
                sendResponse(fromNode, responseMsg);
                delay(100); // Small delay between messages
            }
        }
        else if (strcmp(parts[1], "from") == 0 && partCount > 2) {
            uint32_t targetUser = strtoul(parts[2], nullptr, 16);
            auto messages = logQuery->getMessagesFromUser(targetUser, 5);

            snprintf(responseMsg, sizeof(responseMsg), "Messages from 0x%08x:", targetUser);
            sendResponse(fromNode, responseMsg);

            for (const auto& msg : messages) {
                snprintf(responseMsg, sizeof(responseMsg), "%s", msg.message);
                sendResponse(fromNode, responseMsg);
                delay(100);
            }
        }
        else if (strcmp(parts[1], "dms") == 0) {
            auto messages = logQuery->getDMsWithUser(fromNode, 5);
            sendResponse(fromNode, "Your recent DMs:");

            for (const auto& msg : messages) {
                snprintf(responseMsg, sizeof(responseMsg), "%s: %s", msg.senderName, msg.message);
                sendResponse(fromNode, responseMsg);
                delay(100);
            }
        }
        else if (strcmp(parts[1], "search") == 0 && partCount > 2) {
            auto messages = logQuery->searchMessages(parts[2], 5);
            snprintf(responseMsg, sizeof(responseMsg), "Search results for '%s':", parts[2]);
            sendResponse(fromNode, responseMsg);

            for (const auto& msg : messages) {
                snprintf(responseMsg, sizeof(responseMsg), "%s: %s",  msg.senderName, msg.message);
                sendResponse(fromNode, responseMsg);
                delay(100);
            }
        }
        else if (strcmp(parts[1], "stats") == 0) {
            auto stats = logQuery->getLogStatistics();
            snprintf(responseMsg, sizeof(responseMsg),
                    "Stats: %d msgs, %d DMs, %d users",
                    stats.totalMessages, stats.totalDMs, stats.uniqueUsers);
            sendResponse(fromNode, responseMsg);
        }
    }

    // Clean up allocated strings
    for (int i = 0; i < partCount; i++) {
        free(parts[i]);
    }
}
*/