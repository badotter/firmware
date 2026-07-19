#include "NodeDB.h"
#include "MeshService.h"
#include "configuration.h"
#include <Arduino.h>
#include "./HeltecCommandProcessor.h"
#include "./picoLogger.h"
#include "./OTAUpdateManager.h"

#include "nimble/NimbleBluetooth.h"
NimbleBluetooth *nimbleBluetooth = nullptr;


static const char* heltec_commands[] = {
    "reboot", "ota", "status", "ping", "rebootPico"
};

//NEW: in order to avoid updating the Heltec firmware every time we add a new command, just forward all non-Heltec commands to the Pico.
//static const char* pico_commands[] = {
//    "help", "messages", "setrole", "dms", "node", "stats", "telemetry", "temp", "light", "pressure", "time"
//};

///////////////////////////////////////////////////////////
//OtterNet: this part is nonsense, we will get admin status from database. 
//Send the node ID to the Pico and let it decide the permission level.

// Admin node IDs - these should be configurable or stored in preferences
static const uint32_t ADMIN_NODES[] = {
    0x433dd890,  // Example admin node
    // Add more admin nodes here
};

bool HeltecCommandProcessor::isAdmin(uint32_t node_id) {
    return true;
    //for (int i = 0; i < sizeof(ADMIN_NODES) / sizeof(ADMIN_NODES[0]); i++) {
    //    if (ADMIN_NODES[i] == node_id) {
    //        return true;
    //    }
    //}
    //return false;
}
// end temporary nonsense ///////

bool HeltecCommandProcessor::extractCommand(const char* payload, char* cmd_buf, size_t cmd_size, 
                                             char* args_buf, size_t args_size) {
    if (payload[0] != '/') return false;
    
    const char* p = payload + 1;  // Skip '/'
    
    // Extract command
    size_t i = 0;
    while (*p && *p != ' ' && i < cmd_size - 1) {
        cmd_buf[i++] = *p++;
    }
    cmd_buf[i] = '\0';
    
    // Extract args
    if (*p == ' ') p++;  // Skip space
    i = 0;
    while (*p && i < args_size - 1) {
        args_buf[i++] = *p++;
    }
    args_buf[i] = '\0';
    
    return true;
}

CommandTarget HeltecCommandProcessor::classifyCommand(const char* command) {
    for (auto cmd : heltec_commands)
        if (strcmp(command, cmd) == 0) return TARGET_LOCAL;
    //for (auto cmd : pico_commands)
    //    if (strcmp(command, cmd) == 0) return TARGET_PICO;
    return TARGET_PICO;
}

LocalCommandResult HeltecCommandProcessor::handleReboot(uint32_t from_node) {
    LocalCommandResult result;
    result.handled = true;
    result.should_forward_to_pico = false;
    
    if (!isAdmin(from_node)) {
        result.success = false;
        snprintf(result.response, sizeof(result.response), "Access denied: Admin only");
        return result;
    }
    
    result.success = true;
    snprintf(result.response, sizeof(result.response), "Rebooting in 3 seconds...");
    
    // Schedule reboot after response is sent

    //REBOOT LOGIC HERE

    LOG_INFO("Reboot requested by admin 0x%lx", from_node);
    
    return result;
}

LocalCommandResult HeltecCommandProcessor::handleOTA(uint32_t from_node, const char* args) {
    LocalCommandResult result;
    result.handled = true;
    result.should_forward_to_pico = false;
    
    if (!isAdmin(from_node)) {
        result.success = false;
        snprintf(result.response, sizeof(result.response), "Access denied: Admin only");
        return result;
    }

    // Parse args for OTA mode (e.g., "enable" or "disable")
    if (strcmp(args, "enable") == 0) {
        result.success = true;
        snprintf(result.response, sizeof(result.response), "OTA mode enabled");

        //REBOOT FOR OTA UPDATE LOGIC HERE
        otaManager->startOTAMode();

        // Enable OTA update mode
        LOG_INFO("OTA enabled by admin 0x%lx", from_node);
    } else if (strcmp(args, "disable") == 0) {
        result.success = true;
        otaManager->stopOTAMode();
        snprintf(result.response, sizeof(result.response), "OTA mode disabled");
        LOG_INFO("OTA disabled by admin 0x%lx", from_node);
    } else {
        result.success = false;
        snprintf(result.response, sizeof(result.response), "Usage: /ota <enable|disable>");
    }
    
    return result;
}

LocalCommandResult HeltecCommandProcessor::handleStatus(uint32_t from_node) {
    LocalCommandResult result;
    result.handled = true;
    result.success = true;
    result.should_forward_to_pico = false;
    
    // Get system status
    uint32_t uptime = millis() / 1000;
    char line[80];    
    uint32_t days = uptime / 86400;
    uint32_t hours = (uptime % 86400) / 3600;
    uint32_t mins = (uptime % 3600) / 60;
    uint32_t secs = uptime % 60;
    snprintf(line, sizeof(line),"Uptime: %lud %luh %lum %lus", days, hours, mins, secs);  

    uint32_t heap_free = ESP.getFreeHeap();

    uint32_t battery_mv = analogReadMilliVolts(BATTERY_PIN) * 2;
    
    char line4[80];
    snprintf(line4, sizeof(line4), "Bluetooth: %s", nimbleBluetooth && nimbleBluetooth->isActive() ? "active" : "inactive");
   
    snprintf(result.response, sizeof(result.response), 
             "Uptime: %s, Free heap: %lu bytes, Battery: %lu mV, %s", line, heap_free, battery_mv, line4);
    
    return result;
}

LocalCommandResult HeltecCommandProcessor::handlePing(uint32_t from_node) {
    LocalCommandResult result;
    result.handled = true;
    result.success = true;
    result.should_forward_to_pico = false;
    
    snprintf(result.response, sizeof(result.response), "PONG!");
    
    return result;
}

LocalCommandResult HeltecCommandProcessor::handleRebootPico(uint32_t from_node) {
    LocalCommandResult result;
    result.handled = true;
    result.should_forward_to_pico = false;
    
    result.success = true;
    snprintf(result.response, sizeof(result.response), "Rebooting Pico...");
    
    pinMode(3, OUTPUT);
    digitalWrite(3, LOW);
    delay(300);
    digitalWrite(3, HIGH);
    pinMode(3, INPUT);  // Let Pico drive it again
    
    return result;
}

void HeltecCommandProcessor::forwardToPico(const char* command, uint32_t from_node, uint32_t to_node) {
    // Format: CMD|from_node|to_node|command
    char buffer[512];
    snprintf(buffer, sizeof(buffer), "CMD|%lu|%lu|%s\x04", from_node, to_node, command);
    
    LOG_INFO("Forwarding to Pico: %s", buffer);
    
    //#if defined(PICO_UART_TX) && defined(PICO_UART_RX)
    Serial1.print(buffer);
    //#else
    //LOG_WARN("Pico UART not configured - command not forwarded");
    //#endif
}

void HeltecCommandProcessor::sendResponse(uint32_t target_node, const char* message, bool is_dm) {
    // Allocate packet properly - router manages the memory
    meshtastic_MeshPacket *response = router->allocForSending();
    
    response->to = target_node;
    response->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    response->channel = 0;
    
    if (is_dm) {
        response->want_ack = true;
    }
    
    size_t len = strlen(message);
    if (len > sizeof(response->decoded.payload.bytes) - 1) {
        len = sizeof(response->decoded.payload.bytes) - 1;
    }
    
    memcpy(response->decoded.payload.bytes, message, len);
    response->decoded.payload.size = len;
    
    LOG_INFO("Sending response to 0x%lx: %s", target_node, message);
    
    // Send it - router handles encryption and memory management
    router->sendLocal(response);
}

bool HeltecCommandProcessor::handleCommand(const meshtastic_MeshPacket &mp) {
    auto &p = mp.decoded;
    
    // Extract message text
    char payload[256];
    size_t len = p.payload.size < sizeof(payload) - 1 ? p.payload.size : sizeof(payload) - 1;
    memcpy(payload, p.payload.bytes, len);
    payload[len] = '\0';
    
    // Check if it's a command
    if (payload[0] != '/') {
        return false;  // Not a command
    }
    
    LOG_INFO("Processing command from 0x%lx: %s", mp.from, payload);
    
    // Extract command and args
    char command[64];
    char args[256];
    if (!extractCommand(payload, command, sizeof(command), args, sizeof(args))) {
        LOG_WARN("Failed to parse command");
        return false;
    }
    
    // Classify command
    CommandTarget target = classifyCommand(command);
    
    if (target == TARGET_LOCAL) {
        // Handle locally
        LocalCommandResult result;
        
        if (strcmp(command, "reboot") == 0) {
            result = handleReboot(mp.from);
        } else if (strcmp(command, "ota") == 0) {
            result = handleOTA(mp.from, args);
        } else if (strcmp(command, "status") == 0) {
            result = handleStatus(mp.from);
        } else if (strcmp(command, "ping") == 0) {
            result = handlePing(mp.from);
        } else if (strcmp(command, "rebootPico") == 0) {
            result = handleRebootPico(mp.from);
        } else {
            result.handled = false;
            result.success = false;
            snprintf(result.response, sizeof(result.response), "Unknown local command");
        }
        
        // Send response
        if (result.handled && result.response[0] != '\0') {
            sendResponse(mp.from, result.response, true);
        }
        
        return result.handled;
        
    } else if (target == TARGET_PICO) {
        forwardToPico(payload, mp.from, mp.to);
        return true;
        
    } else {
        // Unknown command
        sendResponse(mp.from, "Unknown command. Try /help", true);
        return true;
    }
}
