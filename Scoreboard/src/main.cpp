#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include "ball_message.h"

#ifndef GOAL_ID
#error Kies GOAL_ID=1 of GOAL_ID=2 in platformio.ini.
#endif
static_assert(GOAL_ID == 1 || GOAL_ID == 2, "Ongeldig doelnummer");

constexpr uint8_t irEmitterPin = 3;
constexpr uint8_t irReceiverPin = 4;
// Wi-Fi STA MAC van de scoreboard-SuperMini op COM5.
constexpr uint8_t receiverAddress[6] = {0x7c, 0x4f, 0xad, 0xc0, 0x8e, 0x64};
constexpr int samplesPerReading = 8;
constexpr int minimumBeamSignal = 60;

uint32_t bootId;
uint32_t ballCount = 0;
int clearBeamSignal = 0;
bool beamBlocked = false;
uint8_t blockedSamples = 0;
uint8_t clearSamples = 0;
uint8_t sendsRemaining = 0;
uint32_t lastSendAt = 0;
volatile uint32_t radioSendSuccess = 0;
volatile uint32_t radioSendFailed = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onSent(const wifi_tx_info_t*, esp_now_send_status_t status) {
#else
void onSent(const uint8_t*, esp_now_send_status_t status) {
#endif
    if (status == ESP_NOW_SEND_SUCCESS) ++radioSendSuccess;
    else ++radioSendFailed;
}

int readAverage() {
    analogRead(irReceiverPin);
    int sum = 0;
    for (int i = 0; i < samplesPerReading; ++i) sum += analogRead(irReceiverPin);
    return sum / samplesPerReading;
}

int measureBeam() {
    digitalWrite(irEmitterPin, LOW);
    delayMicroseconds(1000);
    const int ledOff = readAverage();
    digitalWrite(irEmitterPin, HIGH);
    delayMicroseconds(1000);
    const int ledOn = readAverage();
    digitalWrite(irEmitterPin, LOW);
    return max(0, ledOff - ledOn);
}

bool startRadio() {
    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("Wi-Fi station starten mislukt");
        return false;
    }
    WiFi.setSleep(false);
    delay(100);
    esp_err_t result = esp_wifi_set_channel(radioChannel, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        Serial.printf("Wi-Fi kanaal instellen mislukt: %s\n", esp_err_to_name(result));
        return false;
    }
    uint8_t channel = 0;
    wifi_second_chan_t secondChannel;
    result = esp_wifi_get_channel(&channel, &secondChannel);
    if (result != ESP_OK || channel != radioChannel) {
        Serial.printf("Wi-Fi kanaal fout: %u (%s)\n", channel, esp_err_to_name(result));
        return false;
    }
    result = esp_now_init();
    if (result != ESP_OK) {
        Serial.printf("ESP-NOW starten mislukt: %s\n", esp_err_to_name(result));
        return false;
    }
    esp_now_register_send_cb(onSent);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, receiverAddress, sizeof(receiverAddress));
    peer.channel = 0; // Gebruik het huidige Wi-Fi-kanaal.
    peer.ifidx = WIFI_IF_STA;
    result = esp_now_add_peer(&peer);
    if (result != ESP_OK) {
        Serial.printf("Ontvanger toevoegen mislukt: %s\n", esp_err_to_name(result));
        return false;
    }
    Serial.printf("Radio zender: kanaal %u, MAC %s\n", channel,
                  WiFi.macAddress().c_str());
    return true;
}

void sendBall() {
    BallMessage message = {ballMagic, ballVersion, GOAL_ID, bootId, ballCount};
    const esp_err_t result = esp_now_send(
        receiverAddress, reinterpret_cast<const uint8_t*>(&message), sizeof(message));
    Serial.printf("Doel %d, verstuur bal %lu: %s\n", GOAL_ID,
                  static_cast<unsigned long>(ballCount),
                  result == ESP_OK ? "verzendopdracht geaccepteerd" : "fout");
}

void registerBall() {
    ++ballCount;
    sendsRemaining = 3;
    lastSendAt = 0;
    Serial.printf("Doel %d, bal %lu gedetecteerd\n", GOAL_ID,
                  static_cast<unsigned long>(ballCount));
}

void setup() {
    Serial.begin(115200);
    pinMode(irEmitterPin, OUTPUT);
    digitalWrite(irEmitterPin, LOW);
    pinMode(irReceiverPin, INPUT);
    analogReadResolution(12);

    int sum = 0;
    for (int i = 0; i < 40; ++i) sum += measureBeam();
    clearBeamSignal = sum / 40;
    Serial.printf("Vrije lichtstraal: %d\n", clearBeamSignal);
    if (clearBeamSignal < minimumBeamSignal) {
        Serial.println("IR-signaal te zwak: controleer uitlijning en bedrading");
    }

    bootId = esp_random();
    Serial.println(startRadio() ? "Zender klaar" : "ESP-NOW starten mislukt");
    Serial.println("Test: typ t in de seriele monitor om een balbericht te sturen");
}

void loop() {
    while (Serial.available()) {
        if (Serial.read() == 't') registerBall();
    }
    const int signal = measureBeam();
    static uint8_t calibrationSamples = 0;
    if (clearBeamSignal < minimumBeamSignal) {
        calibrationSamples = signal >= minimumBeamSignal ? calibrationSamples + 1 : 0;
        if (calibrationSamples >= 5) {
            clearBeamSignal = signal;
            calibrationSamples = 0;
            Serial.printf("Lichtstraal gekalibreerd: %d\n", clearBeamSignal);
        }
    } else if (!beamBlocked && signal > clearBeamSignal) {
        clearBeamSignal = signal;
    }
    const bool beamReady = clearBeamSignal >= minimumBeamSignal;
    const int blockThreshold = clearBeamSignal / 2;
    const int releaseThreshold = clearBeamSignal * 7 / 10;

    if (beamReady && !beamBlocked) {
        blockedSamples = signal < blockThreshold ? blockedSamples + 1 : 0;
        if (blockedSamples >= 2) {
            beamBlocked = true;
            clearSamples = 0;
            registerBall();
        }
    } else if (beamBlocked) {
        clearSamples = signal > releaseThreshold ? clearSamples + 1 : 0;
        if (clearSamples >= 2) {
            beamBlocked = false;
            blockedSamples = 0;
        }
    }

    const uint32_t now = millis();
    if (sendsRemaining && (lastSendAt == 0 || now - lastSendAt >= 20)) {
        sendBall();
        lastSendAt = now;
        --sendsRemaining;
    }

    static uint32_t lastDiagnosticAt = 0;
    if (now - lastDiagnosticAt >= 500) {
        lastDiagnosticAt = now;
        Serial.printf("IR=%d basis=%d grens=%d radio_ok=%lu radio_fout=%lu %s\n",
                      signal, clearBeamSignal, blockThreshold,
                      static_cast<unsigned long>(radioSendSuccess),
                      static_cast<unsigned long>(radioSendFailed),
                      !beamReady ? "WACHT OP LICHTSTRAAL" :
                      beamBlocked ? "ONDERBROKEN" : "VRIJ");
    }
}
