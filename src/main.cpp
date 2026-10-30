#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

#if defined(ROLE_CONTROLLER) == defined(ROLE_RECEIVER)
#error Kies precies een rol: ROLE_CONTROLLER of ROLE_RECEIVER.
#endif

#if defined(ROLE_MOTORS) && !defined(ROLE_RECEIVER)
#error ROLE_MOTORS hoort bij ROLE_RECEIVER.
#endif

constexpr uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr uint16_t magic = 0x4C4C;
constexpr uint8_t protocolVersion = 3;  // Bitindeling gewijzigd: upload beide ESP's.
constexpr uint8_t inputType = 1;
constexpr uint8_t ackType = 2;
constexpr uint8_t extraPulseBit = 1 << 0;
constexpr uint8_t stopButtonBit = 1 << 1;
constexpr uint32_t linkTimeoutMs = 200; // Vier verzendintervallen zonder bericht.

struct Message {
    uint16_t magic;
    uint8_t version;
    uint8_t type;
    uint32_t sequence;
    uint16_t x;       // Joystick na kalibratie: 8..4088, midden 2048.
    uint16_t y;
    uint8_t buttons;  // bit 0: extra-knoppuls, bit 1: joystick-stop.
};
static_assert(sizeof(Message) == 16, "Berichtformaat verschilt tussen de ESP's");

QueueHandle_t inbox = nullptr;
bool radioReady = false;
bool connected = false;
uint32_t lastReceived = 0;

#ifdef ROLE_CONTROLLER
constexpr uint8_t joystickX = 0;      // ESP32-C3 GPIO0
constexpr uint8_t joystickY = 1;      // ESP32-C3 GPIO1
constexpr uint8_t joystickButton = 5; // Naar GND bij indrukken
constexpr uint8_t extraButton = 3;    // Naar GND bij indrukken
constexpr int joystickDeadZone = 180;
constexpr uint32_t recalibrationHoldMs = 2000;
int centerX = 2048;
int centerY = 2048;

void calibrateJoystick() {
    uint32_t sumX = 0;
    uint32_t sumY = 0;
    for (int i = 0; i < 48; i++) {
        sumX += analogRead(joystickX);
        sumY += analogRead(joystickY);
        delay(4);
    }
    centerX = sumX / 48;
    centerY = sumY / 48;
    Serial.printf("Joystickmidden: X=%d Y=%d\n", centerX, centerY);
}

// Zet een as om naar -255..255, zonder drift in het midden.
int normalizeAxis(int raw, int center) {
    const int delta = constrain(raw, 0, 4095) - center;
    if (abs(delta) <= joystickDeadZone) return 0;
    if (delta > 0) {
        const int range = max(1, 4095 - center - joystickDeadZone);
        return constrain((delta - joystickDeadZone) * 255 / range, 0, 255);
    }
    const int range = max(1, center - joystickDeadZone);
    return -constrain((-delta - joystickDeadZone) * 255 / range, 0, 255);
}

// Gebruik hetzelfde Message-formaat; stuur gekalibreerde assen.
uint16_t encodeAxis(int normalized) {
    return static_cast<uint16_t>(2048 + constrain(normalized, -255, 255) * 8);
}
#endif

#ifdef ROLE_MOTORS
// D3/D4 en D11/D2 zijn de twee motor-driverkanalen uit de lijnvolgercode.
// Links/rechts volgen de huidige fysieke motoropstelling.
constexpr uint8_t motorLeft = D3;
constexpr uint8_t directionLeft = D4;
constexpr uint8_t motorRight = D11;
constexpr uint8_t directionRight = D2;

// Verander een waarde naar true als een motor verkeerd om loopt.
constexpr bool reverseLeftMotor = false;
constexpr bool reverseRightMotor = false;

// Verander deze waarden als de joystickassen andersom werken.
constexpr bool reverseJoystickX = false;
constexpr bool reverseJoystickY = true; // Omhoog op Y betekent vooruit.

constexpr int rampStep = 10; // PWM-stappen per 10 ms.
int targetLeft = 0;
int targetRight = 0;
int currentLeft = 0;
int currentRight = 0;

// De controller verstuurt -255..255 gecodeerd in 8..4088.
int decodeAxis(uint16_t value) {
    return constrain((static_cast<int>(value) - 2048) / 8, -255, 255);
}

void setMotor(int power, uint8_t pwmPin, uint8_t directionPin, bool reversed) {
    power = constrain(power, -255, 255);
    if (power == 0) {
        analogWrite(pwmPin, 0);
        return;
    }

    const bool forwardDirection = (power > 0) != reversed;
    digitalWrite(directionPin, forwardDirection ? LOW : HIGH);
    analogWrite(pwmPin, abs(power));
}

void drive(int left, int right) {
    setMotor(left, motorLeft, directionLeft, reverseLeftMotor);
    setMotor(right, motorRight, directionRight, reverseRightMotor);
}

void stopMotors() {
    targetLeft = targetRight = 0;
    currentLeft = currentRight = 0;
    drive(0, 0);
}

int rampTowards(int current, int target) {
    // Bij omkeren eerst volledig stoppen.
    if ((current > 0 && target < 0) || (current < 0 && target > 0)) target = 0;
    if (current < target) return min(current + rampStep, target);
    if (current > target) return max(current - rampStep, target);
    return current;
}

void updateMotors() {
    static uint32_t lastUpdate = 0;
    if (millis() - lastUpdate < 10) return;
    lastUpdate = millis();

    const int nextLeft = rampTowards(currentLeft, targetLeft);
    const int nextRight = rampTowards(currentRight, targetRight);
    if (nextLeft == currentLeft && nextRight == currentRight) return;

    currentLeft = nextLeft;
    currentRight = nextRight;
    drive(currentLeft, currentRight);
}

void driveFromJoystick(const Message& input) {
    if (input.buttons & stopButtonBit) { // Joystick indrukken: noodstop.
        stopMotors();
        return;
    }

    int forward = decodeAxis(input.y);
    int turn = decodeAxis(input.x);
    if (reverseJoystickY) forward = -forward;
    if (reverseJoystickX) turn = -turn;

    // Arcade drive: vooruit + sturen. Ook achteruit en op de plek draaien.
    int left = forward + turn;
    int right = forward - turn;

    // Schaal beide motoren evenveel als het totaal > 255 is.
    const int peak = max(abs(left), abs(right));
    if (peak > 255) {
        left = left * 255 / peak;
        right = right * 255 / peak;
    }

    targetLeft = constrain(left, -255, 255);
    targetRight = constrain(right, -255, 255);
}
#endif

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onReceive(const esp_now_recv_info_t*, const uint8_t* data, int length) {
#else
void onReceive(const uint8_t*, const uint8_t* data, int length) {
#endif
    if (length != sizeof(Message) || inbox == nullptr) return;
    Message message;
    memcpy(&message, data, sizeof(message));
    if (message.magic == magic && message.version == protocolVersion) {
        xQueueOverwrite(inbox, &message);
    }
}

bool startRadio() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    if (esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE) != ESP_OK ||
        esp_now_init() != ESP_OK) return false;

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, broadcast, sizeof(broadcast));
    peer.channel = 1;
    if (esp_now_add_peer(&peer) != ESP_OK) return false;

    esp_now_register_recv_cb(onReceive);
    return true;
}

void setup() {
#ifdef ROLE_MOTORS
    pinMode(motorLeft, OUTPUT);
    pinMode(motorRight, OUTPUT);
    pinMode(directionLeft, OUTPUT);
    pinMode(directionRight, OUTPUT);
    stopMotors();
#endif

    Serial.begin(115200);
    delay(500);

#ifdef ROLE_CONTROLLER
    pinMode(joystickButton, INPUT_PULLUP);
    pinMode(extraButton, INPUT_PULLUP);
    analogReadResolution(12);
    Serial.println("Laat joystick los tijdens kalibratie...");
    calibrateJoystick();
    Serial.println("Houd beide knoppen 2 seconden ingedrukt om opnieuw te kalibreren.");
#endif

    inbox = xQueueCreate(1, sizeof(Message));
    if (!inbox) {
        Serial.println("Fout: geen geheugen voor berichten");
        return;
    }
    radioReady = startRadio();
    Serial.println(radioReady ? "ESP-NOW klaar" : "ESP-NOW fout");
}

#ifdef ROLE_CONTROLLER
void controllerLoop(uint32_t now) {
    Message reply;
    if (xQueueReceive(inbox, &reply, 0) == pdTRUE && reply.type == ackType) {
        lastReceived = now;
        connected = true;
    }

    static uint32_t lastSent = 0;
    static uint32_t lastPrinted = 0;
    static uint32_t sequence = 0;
    static uint32_t bothPressedSince = 0;
    static bool recalibrationPending = false;
    static bool extraRawPrevious = false;
    static bool extraStablePressed = false;
    static bool extraPulsePending = false;
    static uint32_t extraRawChangedAt = 0;
    const bool stopPressed = digitalRead(joystickButton) == LOW;
    const bool extraPressed = digitalRead(extraButton) == LOW;
    if (extraPressed != extraRawPrevious) {
        extraRawPrevious = extraPressed;
        extraRawChangedAt = now;
    }
    if (now - extraRawChangedAt >= 30 && extraPressed != extraStablePressed) {
        extraStablePressed = extraPressed;
        if (extraStablePressed && !stopPressed && !recalibrationPending) {
            extraPulsePending = true;
        }
    }
    if (stopPressed && extraPressed) {
        extraPulsePending = false; // De knopcombinatie is alleen voor kalibratie.
        if (bothPressedSince == 0) bothPressedSince = now;
        if (!recalibrationPending && now - bothPressedSince >= recalibrationHoldMs) {
            recalibrationPending = true;
            Serial.println("Kalibratie klaar om te starten: laat beide knoppen en de joystick los.");
        }
    } else {
        bothPressedSince = 0;
    }
    if (recalibrationPending && !stopPressed && !extraPressed) {
        delay(75); // Laat de joystick na het loslaten tot rust komen.
        calibrateJoystick();
        recalibrationPending = false;
    }
    if (now - lastSent >= 50) {
        lastSent = now;
        const int x = recalibrationPending ? 0 : normalizeAxis(analogRead(joystickX), centerX);
        const int y = recalibrationPending ? 0 : normalizeAxis(analogRead(joystickY), centerY);
        Message inputs = {
            magic, protocolVersion, inputType, ++sequence,
            encodeAxis(x), encodeAxis(y),
            static_cast<uint8_t>((extraPulsePending ? extraPulseBit : 0) |
                                 (stopPressed || recalibrationPending ? stopButtonBit : 0))
        };
        if (radioReady &&
            esp_now_send(broadcast,
                         reinterpret_cast<const uint8_t*>(&inputs), sizeof(inputs)) == ESP_OK &&
            extraPulsePending) {
            extraPulsePending = false;
            Serial.println("Extra knop: puls verzonden");
        }
        if (now - lastPrinted >= 200) {
            lastPrinted = now;
            Serial.printf("X=%d Y=%d stop=%u extra_knop=%u extra_puls=%u verbinding=%s\n",
                          x, y,
                          static_cast<unsigned>(bool(inputs.buttons & stopButtonBit)),
                          static_cast<unsigned>(extraPressed),
                          static_cast<unsigned>(bool(inputs.buttons & extraPulseBit)),
                          connected ? "VERBONDEN" : "NIET VERBONDEN");
        }
    }
}
#else
void receiverLoop(uint32_t now) {
    Message inputs;
    if (xQueueReceive(inbox, &inputs, 0) == pdTRUE && inputs.type == inputType) {
        lastReceived = now;
        connected = true;
#ifdef ROLE_MOTORS
        driveFromJoystick(inputs);
#endif
        if (inputs.buttons & extraPulseBit) {
            static uint32_t extraPulseCount = 0;
            Serial.printf("Extra knop: puls ontvangen #%lu\n",
                          static_cast<unsigned long>(++extraPulseCount));
        }
        static uint32_t lastPrinted = 0;
        if (now - lastPrinted >= 200) {
            lastPrinted = now;
            Serial.printf("X=%u Y=%u stop=%u extra_puls=%u",
                          static_cast<unsigned>(inputs.x),
                          static_cast<unsigned>(inputs.y),
                          static_cast<unsigned>(bool(inputs.buttons & stopButtonBit)),
                          static_cast<unsigned>(bool(inputs.buttons & extraPulseBit)));
#ifdef ROLE_MOTORS
            Serial.printf(" doel_links=%d doel_rechts=%d", targetLeft, targetRight);
#endif
            Serial.println();
        }
        Message reply = {magic, protocolVersion, ackType, inputs.sequence, 0, 0, 0};
        esp_now_send(broadcast,
                     reinterpret_cast<const uint8_t*>(&reply), sizeof(reply));
    }

    static uint32_t lastWaitingPrint = 0;
    if (!connected && now - lastWaitingPrint >= 2000) {
        lastWaitingPrint = now;
        Serial.println("Wacht op controller...");
    }
}
#endif

void loop() {
    if (!inbox) {
        delay(1000);
        return;
    }
    const uint32_t now = millis();

#ifdef ROLE_CONTROLLER
    controllerLoop(now);
#else
    receiverLoop(now);
#endif
    if (connected && now - lastReceived > linkTimeoutMs) {
        connected = false;
#ifdef ROLE_MOTORS
        stopMotors();
#endif
        Serial.println("Verbinding verloren");
    }
#ifdef ROLE_MOTORS
    updateMotors();
#endif
    delay(1);
}
