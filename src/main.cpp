
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

// Change this to "B" on the second ESP32
const char* deviceName = "B";

uint8_t broadcastAddress[] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};

// Called when a message is received
void handleMessage(const uint8_t* data, int length) {
    Serial.print("Received: ");
    Serial.write(data, length);
    Serial.println();
}

// Arduino ESP32 version compatibility
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceive(const esp_now_recv_info_t* info,
               const uint8_t* data, int length) {
    handleMessage(data, length);
}
#else
void onReceive(const uint8_t* mac,
               const uint8_t* data, int length) {
    handleMessage(data, length);
}
#endif

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Both boards use the same Wi-Fi channel
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW failed!");
        return;
    }

    esp_now_register_recv_cb(onReceive);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcastAddress, 6);
    peer.channel = 1;
    peer.encrypt = false;

    if (esp_now_add_peer(&peer) != ESP_OK) {
        Serial.println("Adding peer failed!");
        return;
    }

    Serial.println("ESP-NOW ready!");
}

void loop() {
    char message[64];

    snprintf(message, sizeof(message),
             "Hello from ESP32 %s", deviceName);

    esp_err_t result = esp_now_send(
        broadcastAddress,
        (uint8_t*)message,
        strlen(message) + 1
    );

    if (result == ESP_OK) {
        Serial.println("Message queued");
    } else {
        Serial.println("Send failed");
    }

    delay(1000);
}
