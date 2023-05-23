#include <stdint.h>

#define RUMBLE_INCLUDE_FREQUENCY 0

struct SwitchRumbleState {
#if RUMBLE_INCLUDE_FREQUENCY
    uint16_t lowChannelFrequency;
    uint16_t highChannelFrequency;
#endif
    uint8_t lowChannelAmplitude;
    uint8_t highChannelAmplitude;
    uint8_t pulse1Amplitude;
    uint8_t pulse2Amplitude;
    uint8_t pulse3Amplitude;
    uint8_t pulse4Amplitude;
};

void decodeSwitchRumbleState(const uint8_t *encodedRumbleState, SwitchRumbleState *switchRumbleStateOut);