//#ifdef ARCH_ESP32
#include "OTAUpdateManager.h"
#include "configuration.h"
#include "main.h"
#include <esp_wifi.h>
#include <SD.h>
#include <esp_task_wdt.h>

int uploadSizePico = 0;

OTAUpdateManager::OTAUpdateManager() : server(nullptr), otaMode(false), otaModeStartTime(0) {
}

OTAUpdateManager::~OTAUpdateManager() {
    stopOTAMode();
}

String OTAUpdateManager::getOTAPassword() {
    /*
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
    
    //LOG_ERROR("No valid OTA password available");
    */
    return String("h@rdc0d3dP@ss");
}

bool OTAUpdateManager::startOTAMode() {
    if (otaMode) {
        LOG_WARN("Already in OTA mode");
        return false;
    }
    uploadSizePico = 0;
    // Get password from SD card (or fallback)
    apPassword = getOTAPassword();
    if (apPassword.length() < 8) {
        LOG_ERROR("No valid password available - OTA mode not started");
        return false;  // ← This prevents starting AP with empty password
    }

    otaModeStartTime = millis();
    LOG_INFO("Starting OTA update mode...");
    
    //if (nimbleBluetooth) {
    //    LOG_INFO("Shutting down Bluetooth for OTA mode...");
    //    nimbleBluetooth->shutdown();
    //    delay(1000);  // Give it time to shut down
    //}

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
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    
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

    //server->on("/upload", HTTP_POST, 
    //           [this]() { handleUploadFinish(); },
    //           [this]() { handleUpload(); });

    server->on("/upload_pico", HTTP_POST, 
        [this]() { handlePicoUploadFinish(); },
        [this]() { handlePicoUpload(); });

    server->on("/upload_heltec", HTTP_POST,
        [this]() { handleHeltecUploadFinish(); },
        [this]() { handleHeltecUpload(); });
    
    server->on("/progress", HTTP_GET, [this]() {
        LOG_INFO("Progress request: %u bytes", upload_progress); 
        char json[64];
        snprintf(json, sizeof(json), "{\"bytes\":%u}", upload_progress);
        server->send(200, "application/json", json);
    });

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
        <h1>Meshtastic OTA Update</h1>
        <div class="info">
            <strong>Device:</strong> )" + getAPName() + R"(<br>
            <strong>IP:</strong> )" + WiFi.softAPIP().toString() + R"(<br>
            <strong>Uptime:</strong> )" + String((millis() - otaModeStartTime) / 1000) + R"( seconds<br>
            <strong>Progress:</strong> )" + String(upload_progress) + R"( bytes<br>
            <strong>Timeout:</strong> )" + String((OTA_TIMEOUT_MS - (millis() - otaModeStartTime)) / 1000) + R"( seconds remaining
        </div>

        <h3>Step 1: Upload Pico Firmware (.uf2)</h3>
        <input type="file" id="picoFile" accept=".uf2">
        <button onclick="uploadFile('/upload_pico', 'picoFile', 'picoStatus') ">Upload Pico</button>
        <div id="picoStatus" style="margin: 10px 0; font-weight: bold;"></div>

        <h3>Step 2: Upload Heltec Firmware (.bin)</h3>
        <input type="file" id="heltecFile" accept=".bin">
        <button onclick="uploadFile('/upload_heltec', 'heltecFile', 'heltecStatus') ">Upload Heltec</button>
        <div id="heltecStatus" style="margin: 10px 0; font-weight: bold;"></div>

        <script>
        function uploadFile(endpoint, fileInputId, statusId) {
            const fileInput = document.getElementById(fileInputId);
            const statusDiv = document.getElementById(statusId);
            const file = fileInput.files[0];
            
            if (!file) { 
                statusDiv.innerHTML = '<span style="color: red;">Please select a file</span>'; 
                return; 
            }

            const formData = new FormData();
            formData.append('firmware', file);
            
            statusDiv.innerHTML = '<span style="color: blue;">Uploading...</span>';

            fetch(endpoint, { method: 'POST', body: formData })
                .then(r => r.text())
                .then(html => {
                    // Extract just the success message from the HTML response
                    const parser = new DOMParser();
                    const doc = parser.parseFromString(html, 'text/html');
                    const message = doc.querySelector('p').textContent;
                    statusDiv.innerHTML = '<span style="color: green;">✓ ' + message + '</span>';
                })
                .catch(e => {
                    statusDiv.innerHTML = '<span style="color: red;">✗ Upload failed</span>';
                });
        }
        </script>
        
        <div class="info">
            <strong>Warning:</strong> Do not disconnect power during upload!<br>
            Device will automatically reboot after successful upload.
        </div>
    </div>
</body>
</html>
)";
    
    server->send(200, "text/html", html);
}

/*
void OTAUpdateManager::handlePicoUpload() {
    HTTPUpload& upload = server->upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        LOG_INFO("Pico firmware upload started: %s", upload.filename.c_str());
            
        while (Serial1.available()) {
            Serial1.read();
        }
        delay(100);

        // Tell Pico to prepare for firmware upload
        Serial1.print("PICO_FW_START\x04");
        delay(100);  // Give Pico time to open file
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        
        LOG_INFO("Uploading pico chunk of size  %u bytes", upload.currentSize);
        // Stream chunks to Pico
        Serial1.print("PICO_FW|");
        Serial1.print(upload.currentSize);
        Serial1.print("|\x04");  // End header
        delay(10);
        
        // Then send raw binary (no \x04)
        Serial1.write(upload.buf, upload.currentSize);
        delay(20);

    } else if (upload.status == UPLOAD_FILE_END) {
        LOG_INFO("Pico firmware upload complete: %u bytes", upload.totalSize);
        
        // Tell Pico we're done
        Serial1.print("PICO_FW_END\x04");
        delay(100);
    }
}*/

void OTAUpdateManager::handlePicoUpload() {
    HTTPUpload& upload = server->upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        LOG_INFO("Pico firmware upload started: %s", upload.filename.c_str());
        upload_progress = 0;
        Serial1.print("PICO_FW_START\x04");
        delay(100);
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        upload_progress += upload.currentSize;
        esp_task_wdt_reset();
        uploadSizePico += upload.currentSize;

        //Old way - arbitrary delay.
        int upload_delay = 300;
        LOG_INFO("Pico firmware writing chunk of size %u bytes, total %u bytes, delay %u MS", upload.currentSize, uploadSizePico, upload_delay);
        Serial1.write(upload.buf, upload.currentSize);
        delay(upload_delay);

        
        /* New way - with ACK/NAK and checksum

        // Calculate checksum
        uint8_t checksum = 0;
        for (size_t i = 0; i < upload.currentSize; i++) {
            checksum ^= upload.buf[i];
        }
        
        // Send: size (2 bytes) + data + checksum
        Serial1.write((uint8_t)(upload.currentSize >> 8));
        Serial1.write((uint8_t)(upload.currentSize & 0xFF));
        Serial1.write(upload.buf, upload.currentSize);
        Serial1.write(checksum);
        Serial1.flush();
        
        // Wait for ACK (0x06) or NAK (0x15)
        unsigned long start = millis();
        while (millis() - start < 1000) {
            if (Serial1.available()) {
                uint8_t response = Serial1.read();
                if (response == 0x06) {  // ACK - success
                    LOG_DEBUG("ACK, %u bytes", uploadSizePico);
                    return;
                } else if (response == 0x15) {  // NAK - checksum failed
                    LOG_ERROR("NAK - checksum mismatch at %u bytes", uploadSizePico);
                    upload.status = UPLOAD_FILE_ABORTED;
                    return;
                }
            }
            yield();
            esp_task_wdt_reset();
        }
        
        LOG_ERROR("Timeout at %u bytes", uploadSizePico);
        upload.status = UPLOAD_FILE_ABORTED;   */


    } else if (upload.status == UPLOAD_FILE_END) {
        LOG_INFO("Pico firmware upload complete: %u bytes", upload.totalSize);
        // Send magic end sequence (4 x \x04)
        //Not anymore! All this end marker stuff failed, we're just going to wait till the data stops coming.
        //Serial1.write("\x04\xFF\xFE\xFD", 4); 
        uploadSizePico = 0;
        delay(100);
    }
}

void OTAUpdateManager::handlePicoUploadFinish() {
    // Check if upload succeeded (we assume it did if we got here)
    picoFirmwareReady = true;
    
    String html = R"(
<!DOCTYPE html>
<html>
<head><title>Pico Upload Success</title><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body style="font-family: Arial, sans-serif; margin: 40px; text-align: center;">
    <h1 style="color: #28a745;">Pico Firmware Uploaded</h1>
    <p>Pico firmware saved successfully!</p>
    <p>Now upload the Heltec firmware to complete the update.</p>
    <a href="/" style="color: #007bff;">← Back to Upload</a>
</body>
</html>
)";
    
    server->send(200, "text/html", html);
    LOG_INFO("Pico firmware ready for bootloader");
}

void OTAUpdateManager::handleHeltecUpload() {
    HTTPUpload& upload = server->upload();
    
    if (upload.status == UPLOAD_FILE_START) {
        LOG_INFO("OTA Upload started: %s", upload.filename.c_str());
        upload_progress = 0;

        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            LOG_ERROR("OTA begin failed");
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        upload_progress += upload.currentSize;
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

void OTAUpdateManager::handleHeltecUploadFinish() {
    if (Update.hasError()) {
        String html = R"(
<!DOCTYPE html>
<html>
<head><title>Upload Failed</title><meta name="viewport" content="width=device-width, initial-scale=1"></head>
<body style="font-family: Arial, sans-serif; margin: 40px; text-align: center;">
    <h1 style="color: #dc3545;">Upload Failed</h1>
    <p>The firmware upload failed. Please try again.</p>
    <a href="/" style="color: #007bff;">Back to Upload</a>
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
    <h1 style="color: #28a745;">Upload Successful</h1>
    <p>Firmware uploaded successfully!</p>
    <p>The device will reboot in 5 seconds...</p>
    <script>
        setTimeout(function() {
            document.body.innerHTML = '<h1>Rebooting...</h1><p>Device is restarting. You can disconnect now.</p>';
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


