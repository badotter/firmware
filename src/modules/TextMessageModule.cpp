#include "TextMessageModule.h"
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

    //if (cmd == "logs") {
    //    handleLogsCommand(fromNode, parts, toNode);
    //}// else if (cmd == "status") {
    //    handleStatusCommand(fromNode, toNode);
    //}// else if (cmd == "time") {
    //    handleTimeCommand(fromNode, toNode);
    //}
}

void sendResponse(uint32_t toNode, const char* message) {
    /*
    // Create a new mesh packet
    meshtastic_MeshPacket *p = allocMeshPacket();

    // Set up the packet
    p->to = toNode;
    p->from = nodeDB->getNodeNum();
    p->id = generatePacketId();
    p->want_ack = false;  // Don't need ACKs for status responses

    // Set up the payload as a text message
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p->decoded.payload.size = strlen(message);
    memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);

    // Send it via the router
    service.sendToMesh(p, RxSource::RX_SRC_LOCAL);
    */
}