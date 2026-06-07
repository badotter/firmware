#include "PicoLogger.h"
#include "configuration.h"
#include "MeshService.h"
#include "NodeDB.h"
#include <Arduino.h>

static char response_buffer[512];
static bool response_pending = false;

void PicoLogger::sendToSerial(const char* formattedMessage)
{
    //#if defined(PICO_UART_TX) && defined(PICO_UART_RX)
    Serial1.print(formattedMessage);
    //#endif
}

bool PicoLogger::isEncrypted(const meshtastic_MeshPacket &mp)
{
    return (mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag);
}

void PicoLogger::handlePlaintext(const meshtastic_MeshPacket &mp)
{
    auto &p = mp.decoded;
    
    // Extract message text
    char messageText[256];
    size_t len = p.payload.size < sizeof(messageText) - 1 ? p.payload.size : sizeof(messageText) - 1;
    memcpy(messageText, p.payload.bytes, len);
    messageText[len] = '\0';
    
    
    char long_name[40] = "LongName";
    char short_name[8] = "shrt";
    meshtastic_NodeInfoLite* node = nodeDB->getMeshNode(mp.from);
    if (node) {
        strncpy(short_name, node->user.short_name, sizeof(short_name)-1);
        strncpy(long_name, node->user.long_name, sizeof(long_name)-1);
        LOG_INFO("Got names? short_name=%s, long_name=%s", short_name, long_name);
    }

    // Format: MSG|from|to|channel|encrypted|rssi|snr|hoplimit|message|encoding
    char buffer[512];
    snprintf(buffer, sizeof(buffer), 
             "MSG|%u|%u|%d|0|%d|%.1f|%d|%s|plaintext|%s|%s\x04",
             mp.from,           // Sender node ID
             mp.to,             // Recipient (0xFFFFFFFF = broadcast)
             mp.channel,        // Channel index
             mp.rx_rssi,        // RSSI in dBm
             mp.rx_snr,         // SNR in dB
             mp.hop_limit,      // Hops remaining
             messageText,       // Actual message
             short_name,       // Short name
             long_name);         // Long name

    LOG_INFO("handlePlaintext - Sending buffer: %s", buffer);
    sendToSerial(buffer);
}

void PicoLogger::handleEncrypted(const meshtastic_MeshPacket &mp)
{
    // The encrypted field is the struct itself, not a pointer
    const auto &encrypted = mp.encrypted;
    
    char hexPayload[513]; // Max 256 bytes = 512 hex chars + null
    
    size_t maxBytes = (sizeof(hexPayload) - 1) / 2;
    size_t bytesToEncode = encrypted.size < maxBytes ? encrypted.size : maxBytes;
    
    for (size_t i = 0; i < bytesToEncode; i++) {
        snprintf(&hexPayload[i * 2], 3, "%02x", encrypted.bytes[i]);
    }
    hexPayload[bytesToEncode * 2] = '\0';
    
    // Format: MSG|from|to|channel|encrypted|rssi|snr|hoplimit|payload|encoding
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
             "MSG|%u|%u|%d|1|%d|%.1f|%d|%s|hex\x04",
             mp.from,
             mp.to,
             mp.channel,
             mp.rx_rssi,
             mp.rx_snr,
             mp.hop_limit,
             hexPayload);
    
    sendToSerial(buffer);
}

void PicoLogger::handleMessage(const meshtastic_MeshPacket &mp)
{
  if (isEncrypted(mp)) {
      handleEncrypted(mp);
  } else {
      handlePlaintext(mp);
  }
}

void PicoLogger::checkForResponses()
{
    // --- Pico watchdog ---
    static uint32_t last_ping_sent_ms = 0;
    static uint32_t ping_sent_at_ms   = 0;
    static bool     ping_pending      = false;
    const uint32_t  PING_INTERVAL_MS  = 6 * 60 * 1000;   // 6 minutes
    const uint32_t  PING_TIMEOUT_MS   = 5 * 1000;        // 5 seconds

    uint32_t now = millis();

    if (!ping_pending && (now - last_ping_sent_ms >= PING_INTERVAL_MS)) {
        LOG_INFO("Watchdog: sending PING to Pico");
        Serial1.print("PING\x04");
        ping_sent_at_ms  = now;
        last_ping_sent_ms = now;
        ping_pending     = true;
    }

    if (ping_pending && (now - ping_sent_at_ms >= PING_TIMEOUT_MS)) {
        LOG_WARN("Watchdog: Pico did not respond to PING, rebooting...");
        ping_pending = false;
        last_ping_sent_ms = now;  // Reset so we ping again after it comes back up
        pinMode(3, OUTPUT);
        digitalWrite(3, LOW);
        delay(300);
        digitalWrite(3, HIGH);
        pinMode(3, INPUT);
    }

    //#if defined(PICO_UART_TX) && defined(PICO_UART_RX)
    static char uart_buffer[512];
    static int uart_idx = 0;
    
    // Read available data from Pico
    while (Serial1.available()) {
        char c = Serial1.read();
        
        if (c == '\x04') {
            if (uart_idx > 0) {
                uart_buffer[uart_idx] = '\0';
                
                LOG_INFO("Buffer check - first 10 chars: '%.*s' (len=%d)", 10, uart_buffer, uart_idx);
                LOG_INFO("First 5 bytes as hex: %02x %02x %02x %02x %02x", 
                    uart_buffer[0], uart_buffer[1], uart_buffer[2], uart_buffer[3], uart_buffer[4]);

                // Check if this is a response
                if (strncmp(uart_buffer, "RESP|", 5) == 0) {
                    LOG_INFO("!!!!!!!!!!!!!!!!Pico response detected: %s", uart_buffer);
                    handlePicoResponse(uart_buffer);
                }
                // Check for ACK
                else if (strcmp(uart_buffer, "ACK") == 0) {
                    LOG_INFO("Pico acknowledged message");
                }
                else if (strcmp(uart_buffer, "PONG") == 0) {
                    LOG_INFO("Watchdog: Pico PONG received, alive");
                    ping_pending = false;
                }
                else {
                    LOG_INFO("From Pico: %s", uart_buffer);
                }
                
                uart_idx = 0;
            }
        } else if (uart_idx < sizeof(uart_buffer) - 1) {
            uart_buffer[uart_idx++] = c;
        } else {
            // Buffer overflow
            LOG_WARN("UART buffer overflow from Pico");
            uart_idx = 0;
        }
    }
    //#endif
}
void PicoLogger::handlePicoResponse(const char* response)
{
    // Format: RESP|target_node|is_dm|message
    uint32_t target_node;
    int is_dm;
    char message[256];
    
    LOG_INFO("Checking Pico response %s", response);
    
    if (sscanf(response, "RESP|%lu|%d|%255[^\n]", &target_node, &is_dm, message) == 3) {
        LOG_INFO("Sending Pico response to 0x%lx: %s", target_node, message);
        
        // Allocate packet properly - router manages the memory
        meshtastic_MeshPacket *resp = router->allocForSending();
        
        resp->to = target_node;
        resp->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        resp->channel = 0;
        
        if (is_dm) {
            resp->want_ack = true;
        }
        
        size_t len = strlen(message);
        if (len > sizeof(resp->decoded.payload.bytes) - 1) {
            len = sizeof(resp->decoded.payload.bytes) - 1;
        }
        memcpy(resp->decoded.payload.bytes, message, len);
        resp->decoded.payload.size = len;
        
        LOG_INFO("Payload size: %d", resp->decoded.payload.size);
        
        // Send via router - handles encryption and memory
        router->sendLocal(resp);
        
    } else {
        LOG_WARN("Failed to parse Pico response: %s", response);
    }
}