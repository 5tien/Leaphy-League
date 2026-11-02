#pragma once

#include <stdint.h>

constexpr uint16_t ballMagic = 0xB411;
constexpr uint8_t ballVersion = 1;
constexpr uint8_t radioChannel = 6;

struct BallMessage {
    uint16_t magic;
    uint8_t version;
    uint8_t reserved;
    uint32_t bootId;
    uint32_t count;
};
static_assert(sizeof(BallMessage) == 12, "Onverwacht berichtformaat");
