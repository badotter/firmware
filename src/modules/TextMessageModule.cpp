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