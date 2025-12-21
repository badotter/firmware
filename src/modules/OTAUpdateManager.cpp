//#ifdef ARCH_ESP32
#include "OTAUpdateManager.h"
#include "configuration.h"
#include "main.h"
#include <esp_wifi.h>
#include <SD.h>

OTAUpdateManager::OTAUpdateManager() : server(nullptr), otaMode(false), otaModeStartTime(0) {
}

OTAUpdateManager::~OTAUpdateManager() {
    stopOTAMode();
}

String OTAUpdateManager::getOTAPassword() {
    #ifdef HAS_SDCARD
    // Try to read password from SD card
    File passwordFile = SD.open("/ota_password.txt", FILE_READ);
    if (passwordFile) {
        String password = passwordFile.readString();
        passwordFile.close();
        password.trim(); // Remove whitespace/newlines
        
        if (password.length() >= 8) {
            LOG_DEBUG("Using OTA password from SD card");
            return password;
        } else {
            LOG_WARN("Password in /ota_password.txt too short (< 8 chars)");
        }
    } else {
        LOG_WARN("No /ota_password.txt file found on SD card");
    }
    #endif
    
    LOG_ERROR("No valid OTA password available");
    return String("");
}

bool OTAUpdateManager::startOTAMode() {
    if (otaMode) {
        LOG_WARN("Already in OTA mode");
        return false;
    }
    
    // Get password from SD card (or fallback)
    apPassword = getOTAPassword();
    if (apPassword.length() < 8) {
        LOG_ERROR("No valid password available - OTA mode not started");
        return false;  // ← This prevents starting AP with empty password
    }

    otaModeStartTime = millis();
    LOG_INFO("Starting OTA update mode...");
    
    // Stop any existing WiFi connections
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(1000);
    
    // Start Access Point
    WiFi.mode(WIFI_AP);
    String apName = getAPName();
    
    if (!WiFi.softAP(apName.c_str(), apPassword.c_str())) {
        LOG_ERROR("Failed to start WiFi AP");
        return false;
    }
    
    // Configure AP IP
    IPAddress local_IP(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    WiFi.softAPConfig(local_IP, gateway, subnet);
    
    delay(2000); // Let AP stabilize
    
    // *** ADD THIS SECTION - WEB SERVER SETUP ***
    LOG_INFO("Starting web server...");
    server = new WebServer(80);
    
    // Set up routes
    server->on("/", HTTP_GET, [this]() { handleRoot(); });
    server->on("/upload", HTTP_POST, 
               [this]() { handleUploadFinish(); },
               [this]() { handleUpload(); });
    server->onNotFound([this]() { handleNotFound(); });
    
    server->begin();
    LOG_INFO("Web server started on port 80");
    // *** END WEB SERVER SETUP ***
    
    otaMode = true;
    
    LOG_INFO("OTA AP started: %s (password: %s)", apName.c_str(), apPassword.c_str());
    LOG_INFO("Connect to http://%s to upload firmware", WiFi.softAPIP().toString().c_str());
    
    return true;
}

void OTAUpdateManager::stopOTAMode() {
    if (!otaMode) return;
    
    LOG_INFO("Stopping OTA mode...");
    
    if (server) {
        server->stop();
        delete server;
        server = nullptr;
    }
    
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    
    otaMode = false;
    
    LOG_INFO("OTA mode stopped");
}

void OTAUpdateManager::loop() {
    if (!otaMode || !server) return;
    
    server->handleClient();
    
    // Check for timeout
    if (hasTimedOut()) {
        LOG_WARN("OTA mode timed out, returning to normal operation");
        stopOTAMode();
        // Don't reboot on timeout, just return to normal mode
    }
}

bool OTAUpdateManager::hasTimedOut() const {
    return otaMode && (millis() - otaModeStartTime > OTA_TIMEOUT_MS);
}

String OTAUpdateManager::getAPName() const {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    return String("Meshtastic-OTA-") + String(mac[4], HEX) + String(mac[5], HEX);
}

IPAddress OTAUpdateManager::getAPIP() const {
    if (otaMode) {
        return WiFi.softAPIP();
    }
    return IPAddress(0, 0, 0, 0);
}

void OTAUpdateManager::handleRoot() {
    String html = R"(
<!DOCTYPE html>
<html>
<head>
    <title>Meshtastic OTA Update</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 40px; background: #f0f0f0; }
        .container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); max-width: 500px; margin: 0 auto; }
        h1 { color: #333; text-align: center; }
        .upload-area { border: 2px dashed #ccc; border-radius: 8px; padding: 40px; text-align: center; margin: 20px 0; }
        .upload-area:hover { border-color: #999; }
        input[type="file"] { margin: 20px 0; }
        button { background: #007bff; color: white; border: none; padding: 12px 24px; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background: #0056b3; }
        .info { background: #e9ecef; padding: 15px; border-radius: 4px; margin: 20px 0; }
        .progress { display: none; margin: 20px 0; }
        .progress-bar { width: 100%; height: 20px; background: #f0f0f0; border-radius: 10px; overflow: hidden; }
        .progress-fill { height: 100%; background: #007bff; width: 0%; transition: width 0.3s; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔄 Meshtastic OTA Update</h1>
        <div class="info">
            <strong>Device:</strong> )" + getAPName() + R"(<br>
            <strong>IP:</strong> )" + WiFi.softAPIP().toString() + R"(<br>
            <strong>Uptime:</strong> )" + String((millis() - otaModeStartTime) / 1000) + R"( seconds<br>
            <strong>Timeout:</strong> )" + String((OTA_TIMEOUT_MS - (millis() - otaModeStartTime)) / 1000) + R"( seconds remaining
        </div>
        
        <form method="POST" action="/upload" enctype="multipart/form-data">
            <div class="upload-area">
                <h3>📁 Select Firmware File</h3>
                <p>Choose a .bin firmware file to upload</p>
                <input type="file" name="firmware" accept=".bin" required>
                <br>
                <button type="submit">🚀 Upload Firmware</button>
            </div>
        </form>
        
        <div class="progress" id="progress">
            <p>Uploading firmware...</p>
            <div class="progress-bar">
                <div class="progress-fill" id="progressFill"></div>
            </div>
        </div>
        
        <div class="info">
            ⚠️ <strong>Warning:</strong> Do not disconnect power during upload!<br>
            Device will automatically reboot after successful upload.
        </div>
    </div>
    
    <script>
        document.querySelector('form').addEventListener('submit', function() {
            document.getElementById('progress').style.display = 'block';
            // Simple progress simulation since we can't track real progress easily
            let progress = 0;
            const interval = setInterval(function() {
                progress += Math.random() * 10;
                if (progress > 90) progress = 90;
                document.getElementById('progressFill').style.width = progress + '%';
            }, 500);
        });
    </script>
</body>
</html>
)";
    
    server->send(200, "text/html", html);
}

void OTAUpdateManager::handleUpload() {
    HTTPUpload& upload = server->upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        LOG_INFO("OTA Upload started: %s", upload.filename.c_str());
        
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOG_ERROR("OTA begin failed");
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            LOG_ERROR("OTA write failed");
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {
            LOG_INFO("OTA upload successful: %u bytes", upload.totalSize);
        } else {
            LOG_ERROR("OTA end failed");
            Update.printError(Serial);
        }
    }
}

void OTAUpdateManager::handleUploadFinish() {
    if (Update.hasError()) {
        String html = R"(
<!DOCTYPE html>
<html>
<head><title>Upload Failed</title><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body style="font-family: Arial, sans-serif; margin: 40px; text-align: center;">
    <h1 style="color: #dc3545;">❌ Upload Failed</h1>
    <p>The firmware upload failed. Please try again.</p>
    <a href="/" style="color: #007bff;">← Back to Upload</a>
</body>
</html>
)";
        server->send(500, "text/html", html);
        LOG_ERROR("OTA upload failed");
    } else {
        String html = R"(
<!DOCTYPE html>
<html>
<head><title>Upload Success</title><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body style="font-family: Arial, sans-serif; margin: 40px; text-align: center;">
    <h1 style="color: #28a745;">✅ Upload Successful</h1>
    <p>Firmware uploaded successfully!</p>
    <p>The device will reboot in 5 seconds...</p>
    <script>
        setTimeout(function() {
            document.body.innerHTML = '<h1>🔄 Rebooting...</h1><p>Device is restarting. You can disconnect now.</p>';
        }, 3000);
    </script>
</body>
</html>
)";
        server->send(200, "text/html", html);
        
        LOG_INFO("OTA upload successful, rebooting in 5 seconds...");
        
        // Schedule reboot
        delay(5000);
        ESP.restart();
    }
}

void OTAUpdateManager::handleNotFound() {
    server->send(404, "text/plain", "File not found");
}

//#endif // ARCH_ESP32