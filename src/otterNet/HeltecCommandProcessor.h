#pragma once
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <Arduino.h>

// Command categories
enum CommandTarget {
    TARGET_LOCAL,     // Handle on Heltec (e.g., reboot, OTA)
    TARGET_PICO,      // Forward to Pico (e.g., database queries)
    TARGET_UNKNOWN
};

// Local command result
struct LocalCommandResult {
    bool handled;
    bool success;
    char response[256];
    bool should_forward_to_pico;
};

class HeltecCommandProcessor {
public:
    // Process incoming command message
    static bool handleCommand(const meshtastic_MeshPacket &mp);
    
private:
    // Determine where command should be processed
    static CommandTarget classifyCommand(const char* command);
    
    // Local command handlers (Heltec-side)
    static LocalCommandResult handleReboot(uint32_t from_node);
    static LocalCommandResult handleOTA(uint32_t from_node, const char* args);
    static LocalCommandResult handleStatus(uint32_t from_node);
    static LocalCommandResult handlePing(uint32_t from_node);
    static LocalCommandResult handleRebootPico(uint32_t from_node);
    
    // Authorization
    static bool isAdmin(uint32_t node_id);
    
    // Communication with Pico
    static void forwardToPico(const char* command, uint32_t from_node, uint32_t to_node);
    static void sendResponse(uint32_t target_node, const char* message, bool is_dm = true);
    
    // Utilities
    static bool extractCommand(const char* payload, char* cmd_buf, size_t cmd_size, 
                               char* args_buf, size_t args_size);
};
