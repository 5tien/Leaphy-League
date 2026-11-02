#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include "ball_message.h"

// 5461BS-1 common-anode display. Elk segment heeft een eigen 330-ohm weerstand.
constexpr uint8_t segmentPins[] = {0, 1, 2, 3, 4, 5, 6, 7};
constexpr uint8_t digitPins[] = {9, 10, 20, 21};
constexpr uint8_t digitSegments[10][8] = {
    {1, 1, 1, 1, 1, 1, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {1, 1, 0, 1, 1, 0, 1, 0},
    {1, 1, 1, 1, 0, 0, 1, 0},
    {0, 1, 1, 0, 0, 1, 1, 0},
    {1, 0, 1, 1, 0, 1, 1, 0},
    {1, 0, 1, 1, 1, 1, 1, 0},
    {1, 1, 1, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 0},
    {1, 1, 1, 1, 0, 1, 1, 0},
};
uint8_t goalScores[2] = {0, 0};
bool displayEnabled = true;
constexpr uint32_t scoreCooldownMs = 5000;
uint32_t lastScoreAt[2] = {0, 0};
bool hasScored[2] = {false, false};

void incrementScore(uint8_t goalId) {
    if (goalId < 1 || goalId > 2) return;
    const uint8_t index = goalId - 1;
    const uint32_t now = millis();
    if (hasScored[index] && now - lastScoreAt[index] < scoreCooldownMs) {
        Serial.printf("Doel %u: bal genegeerd, wacht 5 seconden\n", goalId);
        return;
    }
    lastScoreAt[index] = now;
    hasScored[index] = true;
    goalScores[index] = (goalScores[index] + 1) % 100;
    Serial.printf("Score: %02u-%02u\n", goalScores[0], goalScores[1]);
}

void displayOneDigit(uint8_t position) {
    for (uint8_t pin : digitPins) digitalWrite(pin, HIGH);

    const uint8_t score = goalScores[position / 2];
    const uint8_t value = position % 2 == 0 ? score / 10 : score % 10;
    for (uint8_t segment = 0; segment < 8; ++segment) {
        digitalWrite(segmentPins[segment], digitSegments[value][segment] ? LOW : HIGH);
    }

    digitalWrite(digitPins[position], LOW);
    delay(2);
    digitalWrite(digitPins[position], HIGH);
}

QueueHandle_t messages = nullptr;
volatile uint32_t packetsSeen = 0;
volatile uint32_t validPacketsSeen = 0;
volatile uint32_t wrongLengthSeen = 0;
volatile uint32_t wrongMagicSeen = 0;
volatile uint32_t wrongVersionSeen = 0;
volatile int lastLengthSeen = 0;
volatile uint16_t lastMagicSeen = 0;
volatile uint8_t lastVersionSeen = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceive(const esp_now_recv_info_t*, const uint8_t* data, int length) {
#else
void onReceive(const uint8_t*, const uint8_t* data, int length) {
#endif
    ++packetsSeen;
    lastLengthSeen = length;
    if (length != sizeof(BallMessage) || !messages) {
        ++wrongLengthSeen;
        return;
    }
    BallMessage message;
    memcpy(&message, data, sizeof(message));
    lastMagicSeen = message.magic;
    lastVersionSeen = message.version;
    if (message.magic != ballMagic) {
        ++wrongMagicSeen;
        return;
    }
    if (message.version != ballVersion) {
        ++wrongVersionSeen;
        return;
    }
    if (message.goalId < 1 || message.goalId > 2) return;
    ++validPacketsSeen;
    xQueueSend(messages, &message, 0);
}

void setup() {
    Serial.begin(115200);
    for (uint8_t pin : digitPins) {
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
    }
    for (uint8_t pin : segmentPins) {
        digitalWrite(pin, HIGH);
        pinMode(pin, OUTPUT);
    }
    messages = xQueueCreate(8, sizeof(BallMessage));
    if (!messages) {
        Serial.println("Geen geheugen voor berichten");
        return;
    }
    if (!WiFi.mode(WIFI_STA)) {
        Serial.println("Wi-Fi station starten mislukt");
        return;
    }
    WiFi.setSleep(false);
    delay(100);
    esp_err_t result = esp_wifi_set_channel(radioChannel, WIFI_SECOND_CHAN_NONE);
    if (result != ESP_OK) {
        Serial.printf("Wi-Fi kanaal instellen mislukt: %s\n", esp_err_to_name(result));
        return;
    }
    uint8_t channel = 0;
    wifi_second_chan_t secondChannel;
    result = esp_wifi_get_channel(&channel, &secondChannel);
    if (result != ESP_OK || channel != radioChannel) {
        Serial.printf("Wi-Fi kanaal fout: %u (%s)\n", channel, esp_err_to_name(result));
        return;
    }
    result = esp_now_init();
    if (result != ESP_OK) {
        Serial.printf("ESP-NOW starten mislukt: %s\n", esp_err_to_name(result));
        return;
    }
    esp_now_register_recv_cb(onReceive);
    Serial.printf("Radio ontvanger: kanaal %u, MAC %s\n", channel,
                  WiFi.macAddress().c_str());
    Serial.println("Wacht op balpassage...");
    Serial.println("Displaytest: typ 1 of 2 in de seriele monitor");
}

void loop() {
    static uint32_t lastStatusAt = 0;
    if (millis() - lastStatusAt >= 2000) {
        lastStatusAt = millis();
        uint8_t channel = 0;
        wifi_second_chan_t secondChannel;
        const esp_err_t channelResult = esp_wifi_get_channel(&channel, &secondChannel);
        Serial.printf("Radio: MAC=%s kanaal=%u status=%s, totaal=%lu geldig=%lu lengte_fout=%lu magic_fout=%lu versie_fout=%lu laatste=%d/%04X/%u\n",
                      WiFi.macAddress().c_str(), channel, esp_err_to_name(channelResult),
                      static_cast<unsigned long>(packetsSeen),
                      static_cast<unsigned long>(validPacketsSeen),
                      static_cast<unsigned long>(wrongLengthSeen),
                      static_cast<unsigned long>(wrongMagicSeen),
                      static_cast<unsigned long>(wrongVersionSeen),
                      lastLengthSeen, lastMagicSeen, lastVersionSeen);
    }
    while (Serial.available()) {
        const char command = Serial.read();
        if (command == '1' || command == '2') incrementScore(command - '0');
        if (command == 'd') {
            displayEnabled = !displayEnabled;
            for (uint8_t pin : digitPins) digitalWrite(pin, HIGH);
            for (uint8_t pin : segmentPins) digitalWrite(pin, HIGH);
            Serial.printf("Display %s\n", displayEnabled ? "aan" : "uit");
        }
    }
    static uint32_t previousBootId[2] = {0, 0};
    static uint32_t previousCount[2] = {0, 0};
    static bool receivedAny[2] = {false, false};
    BallMessage message;
    while (messages && xQueueReceive(messages, &message, 0) == pdTRUE) {
        const uint8_t index = message.goalId - 1;
        if (!receivedAny[index] || message.bootId != previousBootId[index] ||
            message.count > previousCount[index]) {
            receivedAny[index] = true;
            previousBootId[index] = message.bootId;
            previousCount[index] = message.count;
            incrementScore(message.goalId);
        }
    }

    if (displayEnabled) {
        for (uint8_t position = 0; position < 4; ++position) {
            displayOneDigit(position);
        }
    } else {
        delay(8);
    }
}
