#include "TextMessageModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "buzz.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "../OtterNet/picoLogger.h"
#include "../OtterNet/HeltecCommandProcessor.h"

TextMessageModule *textMessageModule;

ProcessMessage TextMessageModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(DEBUG_PORT) && !defined(DEBUG_MUTE)
    auto &p = mp.decoded;
    LOG_INFO("Received text msg from=0x%0x, id=0x%x, msg=%.*s", mp.from, mp.id, p.payload.size, p.payload.bytes);
#endif

    // OtterNet Pico Interface  /////////////////////////////////////////////
    // Extract payload for command detection
    char payload[256];
    size_t len = mp.decoded.payload.size < sizeof(payload) - 1 ?
                 mp.decoded.payload.size : sizeof(payload) - 1;
    memcpy(payload, mp.decoded.payload.bytes, len);
    payload[len] = '\0';

    // Check if this is a command
    if (payload[0] == '/') {
        LOG_INFO("Command detected, processing...");

        // Try to handle as command
        if (HeltecCommandProcessor::handleCommand(mp)) {
            LOG_INFO("Command handled");
            // Command was processed - don't store it as a regular message
            // Still wake screen and notify observers
            if (shouldWakeOnReceivedMessage()) {
                powerFSM.trigger(EVENT_RECEIVED_MSG);
            }
            notifyObservers(&mp);
            return ProcessMessage::STOP;  // Don't pass to other handlers
        }
    }

    // Regular message - log it
    PicoLogger::handleMessage(mp);

    //End OtterNet Pico Interface  /////////////////////////////////////////

    // We only store/display messages destined for us.
    // Keep a copy of the most recent text message.
    devicestate.rx_text_message = mp;
    devicestate.has_rx_text_message = true;

    // Only trigger screen wake if configuration allows it
    if (shouldWakeOnReceivedMessage()) {
        powerFSM.trigger(EVENT_RECEIVED_MSG);
    }
    notifyObservers(&mp);

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool TextMessageModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return MeshService::isTextPayload(p);
}
