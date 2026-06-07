#pragma once
#include "mesh/generated/meshtastic/mesh.pb.h"

class PicoLogger {
public:
    static void handleMessage(const meshtastic_MeshPacket &mp);    
    static void checkForResponses();//from Pico
    
private:
    static void sendToSerial(const char* formattedMessage);
    static bool isEncrypted(const meshtastic_MeshPacket &mp);
    static void handleEncrypted(const meshtastic_MeshPacket &mp);
    static void handlePlaintext(const meshtastic_MeshPacket &mp);
    static void handlePicoResponse(const char* response);
};