#pragma once

//#ifdef ARCH_ESP32
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Arduino.h>

class OTAUpdateManager {
private:
    WebServer* server;
    bool otaMode;
    uint32_t otaModeStartTime;
    static const uint32_t OTA_TIMEOUT_MS = 10 * 60 * 1000; // 10 minutes
    String apPassword;
    
    void handleRoot();
    void handleUpload();
    void handleUploadFinish();
    void handleNotFound();
    
public:
    OTAUpdateManager();
    ~OTAUpdateManager();
    
    bool startOTAMode();
    void stopOTAMode();
    void loop(); // Call this in main loop when in OTA mode
    bool isInOTAMode() const { return otaMode; }
    bool hasTimedOut() const;
    
    // Get AP info for user
    String getAPName() const;
    String getAPPassword() const { return apPassword; }
    IPAddress getAPIP() const;
    uint32_t getStartTime() const { return otaModeStartTime; }
    
    // Password management
    String getOTAPassword();
};

extern OTAUpdateManager* otaManager;

//#endif // ARCH_ESP32