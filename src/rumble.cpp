// Rumble resources:
//
// Current decoding based on information here:
// https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/issues/11
//
// Original rumble translation with help from MissionControl:
// https://github.com/ndeadly/MissionControl/blob/master/mc_mitm/source/controllers/emulated_switch_controller.cpp
//
// Which in turn got them from dekunukem repo normalised and scaled by function used by yuzu
// https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/rumble_data_table.md
// https://github.com/yuzu-emu/yuzu/blob/d3a4a192fe26e251f521f0311b2d712f5db9918e/src/input_common/sdl/sdl_impl.cpp#L429

#include "rumble.h"
#include "serial.h"
#include "packedStrings.h"
#include <avr/builtins.h>
#include <avr/pgmspace.h>
#include <string.h>
#include <assert.h>
#include <math.h>

/*
static const PROGMEM uint16_t rumble_freq_lut[] = {
    0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f, 0x0030, 0x0031,
    0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0039, 0x003a, 0x003b,
    0x003c, 0x003e, 0x003f, 0x0040, 0x0042, 0x0043, 0x0045, 0x0046, 0x0048,
    0x0049, 0x004b, 0x004d, 0x004e, 0x0050, 0x0052, 0x0054, 0x0055, 0x0057,
    0x0059, 0x005b, 0x005d, 0x005f, 0x0061, 0x0063, 0x0066, 0x0068, 0x006a,
    0x006c, 0x006f, 0x0071, 0x0074, 0x0076, 0x0079, 0x007b, 0x007e, 0x0081,
    0x0084, 0x0087, 0x0089, 0x008d, 0x0090, 0x0093, 0x0096, 0x0099, 0x009d,
    0x00a0, 0x00a4, 0x00a7, 0x00ab, 0x00ae, 0x00b2, 0x00b6, 0x00ba, 0x00be,
    0x00c2, 0x00c7, 0x00cb, 0x00cf, 0x00d4, 0x00d9, 0x00dd, 0x00e2, 0x00e7,
    0x00ec, 0x00f1, 0x00f7, 0x00fc, 0x0102, 0x0107, 0x010d, 0x0113, 0x0119,
    0x011f, 0x0125, 0x012c, 0x0132, 0x0139, 0x0140, 0x0147, 0x014e, 0x0155,
    0x015d, 0x0165, 0x016c, 0x0174, 0x017d, 0x0185, 0x018d, 0x0196, 0x019f,
    0x01a8, 0x01b1, 0x01bb, 0x01c5, 0x01ce, 0x01d9, 0x01e3, 0x01ee, 0x01f8,
    0x0203, 0x020f, 0x021a, 0x0226, 0x0232, 0x023e, 0x024b, 0x0258, 0x0265,
    0x0272, 0x0280, 0x028e, 0x029c, 0x02ab, 0x02ba, 0x02c9, 0x02d9, 0x02e9,
    0x02f9, 0x030a, 0x031b, 0x032c, 0x033e, 0x0350, 0x0363, 0x0376, 0x0389,
    0x039d, 0x03b1, 0x03c6, 0x03db, 0x03f1, 0x0407, 0x041d, 0x0434, 0x044c,
    0x0464, 0x047d, 0x0496, 0x04af, 0x04ca, 0x04e5
};
*/

enum RumbleStateType {
    RumbleStateTypeX0SingleWaveWithResonance = 0x10,
    RumbleStateType0100DualWave = 0x0111,
    RumbleStateType0101Silent = 0x0101,
    RumbleStateType0110DualResonanceWith3Pulse = 0x0110,
    RumbleStateType11DualResonanceWith4Pulse = 0x11,
    RumbleStateTypeUnrecognized = 0xff
};

static RumbleStateType rumbleStateTypeFromEncodedRumbleState(const uint8_t *encodedRumbleState)
{
    switch((encodedRumbleState[3] & 0b11000000) >> 6) {
    case 0b00:
    case 0b10:
        return RumbleStateTypeX0SingleWaveWithResonance;
    case 0b01:
        switch(encodedRumbleState[0] & 0b00000011) {
        case 0b00:
            return RumbleStateType0100DualWave;
        case 0b01:
            return RumbleStateType0101Silent;
        case 0b10:
            return RumbleStateType0110DualResonanceWith3Pulse;
        }
        break;
    case 0b11:
        return RumbleStateType11DualResonanceWith4Pulse;
    }

    return RumbleStateTypeUnrecognized;
}

struct __attribute__((packed)) RumbleStateX0SingleWaveWithResonance {
    uint8_t type:2;

    uint8_t pulse2Amplitude7Bit:7;

    bool pulse1Switch:1;
    uint8_t pulse1Amplitude4Bit:4;

    bool lowChannelSwitch:1;
    uint8_t lowChannelAmplitude4Bit:4;

    bool highChannelSwitch:1;
    uint8_t highChannelAmplitude4Bit:4;

    bool highLowSelect:1;
    uint8_t frequency7Bit:4;
};
static_assert(sizeof(RumbleStateX0SingleWaveWithResonance) == 4, "RumbleStateX0SingleWaveWithResonance fields incorrect or incorrectly packed");

struct __attribute__((packed)) RumbleState0100DualWave {
    uint8_t type:2;

    uint8_t lowChannelAmplitude7Bit:7;
    uint8_t lowChannelFrequency7Bit:7;

    uint8_t highChannelAmplitude7Bit:7;
    uint8_t highChannelFrequency7Bit:7;

    uint8_t subType:2;
};
static_assert(sizeof(RumbleState0100DualWave) == 4, "RumbleState0100DualWave fields incorrect or incorrectly packed");

struct __attribute__((packed)) RumbleState0110DualResonanceWith3Pulse {
    uint8_t type:2;

    uint8_t pulse3Amplitude7Bit:7;

    bool pulse2Switch:1;
    uint8_t pulse2Amplitude4Bit:4;

    bool pulse1Switch:1;
    uint8_t pulse1Amplitude4Bit:4;

    bool lowChannelSwitch:1;
    uint8_t lowChannelAmplitude4Bit:4;

    bool highChannelSwitch:1;
    uint8_t highChannelAmplitude4Bit:4;

    uint8_t unknown:1;
    uint8_t subType:2;
};
static_assert(sizeof(RumbleState0110DualResonanceWith3Pulse) == 4, "RumbleState0110DualResonanceWith3Pulse fields incorrect or incorrectly packed");

struct __attribute__((packed)) RumbleState11DualResonanceWith4Pulse {
    uint8_t type:2;

    bool pulse4Switch:1;
    uint8_t pulse4Amplitude4Bit:4;

    bool pulse3Switch:1;
    uint8_t pulse3Amplitude4Bit:4;

    bool pulse2Switch:1;
    uint8_t pulse2Amplitude4Bit:4;

    bool pulse1Or400HzSwitch:1;
    uint8_t pulse1Or400HzAmplitude4Bit:4;

    bool lowChannelSwitch:1;
    uint8_t lowChannelAmplitude4Bit:4;

    bool highChannelSwitch:1;
    uint8_t highChannelAmplitude4Bit:4;
};
static_assert(sizeof(RumbleState11DualResonanceWith4Pulse) == 4, "RumbleStateType11DualResonanceWith4Pulse fields incorrect or incorrectly packed");

// Weird extern declaration to keep VS Code happy. It's not actually necessary
// for compilation, but Intellisense can't find a declaration when editing
extern uint8_t (__builtin_avr_insert_bits)(uint32_t, uint8_t, uint8_t);
static uint8_t reverseBits(const uint8_t num) {
    return __builtin_avr_insert_bits(0x01234567, num, 0);
}

static uint8_t amplitudeFrom4BitAmplitude(const uint8_t fourBitValue)
{
    // From https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/issues/11
    static const PROGMEM uint8_t amplitude_lut[16] = {
        (uint8_t)(0 * 0xff),
        (uint8_t)(1 * 0xff),
        (uint8_t)(0.713429339 * 0xff),
        (uint8_t)(0.510491764 * 0xff),
        (uint8_t)(0.364076932 * 0xff),
        (uint8_t)(0.263212876 * 0xff),
        (uint8_t)(0.187285343 * 0xff),
        (uint8_t)(0.128740086 * 0xff),
        (uint8_t)(0.096642284 * 0xff),
        (uint8_t)(0.065562582 * 0xff),
        (uint8_t)(0.047502641 * 0xff),
        (uint8_t)(0.035863824 * 0xff),
    };
    if(fourBitValue < sizeof(amplitude_lut)) {
        return pgm_read_byte(&amplitude_lut[fourBitValue]);
    } else {
        return 0;
    }
}

static uint8_t amplitudeFrom7BitAmplitude(uint8_t sevenBitValue)
{
    // There's actually a weird curve here - see:
    // https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/rumble_data_table.md#amplitude-table
    //
    // But it's not worth dealing with it for the resolution we'd get out of
    // Dual Shock motors.
    //
    // Here it is in ascii art - x is 'sevenBitValue', Y is amplitude from
    // 0 to 1.
    //
    // ~> pbpaste | youplot lineplot -w67 -h18 --canvas dot --border ascii  -o - | sed 's/.*/     \/\/&/'
    //     +-------------------------------------------------------------------+
    //   1 |                                                           ▄▛▘     |
    //     |                                                        ▗▞▀        |
    //     |                                                     ▄▄▀▘          |
    //     |                                                  ▄▄▀              |
    //     |                                              ▗▄▞▀                 |
    //     |                                           ▄▞▀▘                    |
    //     |                                      ▗▄▞▀▀                        |
    //     |                                  ▄▄▀▀▘                            |
    //     |                           ▗▄▄▞▀▀▀                                 |
    //     |                     ▄▄▄▞▀▀▘                                       |
    //     |                ▄▄▞▀▀                                              |
    //     |           ▗▄▄▞▀▘                                                  |
    //     |         ▞▀▘                                                       |
    //     |      ▗▞▀                                                          |
    //     |    ▄▀▀                                                            |
    //     | ▄▀▀                                                               |
    //     | ▌                                                                 |
    //   0 |▐                                                                  |
    //     +-------------------------------------------------------------------+
    //     0


    return sevenBitValue << 1;
}

#if RUMBLE_INCLUDE_FREQUENCY
static uint16_t frequencyFrom7BitFrequency(uint8_t sevenBitValue)
{
    static const PROGMEM uint16_t rumble_freq_lut[] = {
        0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f, 0x0030, 0x0031,
        0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037, 0x0039, 0x003a, 0x003b,
        0x003c, 0x003e, 0x003f, 0x0040, 0x0042, 0x0043, 0x0045, 0x0046, 0x0048,
        0x0049, 0x004b, 0x004d, 0x004e, 0x0050, 0x0052, 0x0054, 0x0055, 0x0057,
        0x0059, 0x005b, 0x005d, 0x005f, 0x0061, 0x0063, 0x0066, 0x0068, 0x006a,
        0x006c, 0x006f, 0x0071, 0x0074, 0x0076, 0x0079, 0x007b, 0x007e, 0x0081,
        0x0084, 0x0087, 0x0089, 0x008d, 0x0090, 0x0093, 0x0096, 0x0099, 0x009d,
        0x00a0, 0x00a4, 0x00a7, 0x00ab, 0x00ae, 0x00b2, 0x00b6, 0x00ba, 0x00be,
        0x00c2, 0x00c7, 0x00cb, 0x00cf, 0x00d4, 0x00d9, 0x00dd, 0x00e2, 0x00e7,
        0x00ec, 0x00f1, 0x00f7, 0x00fc, 0x0102, 0x0107, 0x010d, 0x0113, 0x0119,
        0x011f, 0x0125, 0x012c, 0x0132, 0x0139, 0x0140, 0x0147, 0x014e, 0x0155,
        0x015d, 0x0165, 0x016c, 0x0174, 0x017d, 0x0185, 0x018d, 0x0196, 0x019f,
        0x01a8, 0x01b1, 0x01bb, 0x01c5, 0x01ce, 0x01d9, 0x01e3, 0x01ee, 0x01f8,
        0x0203, 0x020f, 0x021a, 0x0226, 0x0232, 0x023e, 0x024b, 0x0258, 0x0265,
        0x0272, 0x0280, 0x028e, 0x029c, 0x02ab, 0x02ba, 0x02c9, 0x02d9, 0x02e9,
        0x02f9, 0x030a, 0x031b, 0x032c, 0x033e, 0x0350, 0x0363, 0x0376, 0x0389,
        0x039d, 0x03b1, 0x03c6, 0x03db, 0x03f1, 0x0407, 0x041d, 0x0434, 0x044c,
        0x0464, 0x047d, 0x0496, 0x04af, 0x04ca, 0x04e5
    };

    if(sevenBitValue < sizeof(rumble_freq_lut)) {
        return pgm_read_byte(&rumble_freq_lut[sevenBitValue]);
    } else {
        return pgm_read_byte(&rumble_freq_lut[sizeof(rumble_freq_lut) - 1]);
    }
}
#endif

void decodeSwitchRumbleState(const uint8_t *encodedRumbleState, SwitchRumbleState *switchRumbleStateOut)
{
    memset(switchRumbleStateOut, 0, sizeof(SwitchRumbleState));

    RumbleStateType rumbleStateType = rumbleStateTypeFromEncodedRumbleState(encodedRumbleState);

    debugPrintStr6(STR6(" <"));
    debugPrintHex(rumbleStateType >> 8);
    debugPrintHex(rumbleStateType & 0xFF);
    debugPrint('>');

    // Ugh. Literally everything about the order of this buffer - both the bit
    // order and the byte order - are the wrong way round for AVR...
    const uint8_t rumbleStateFlipped[] = { reverseBits(encodedRumbleState[3]),
                                           reverseBits(encodedRumbleState[2]),
                                           reverseBits(encodedRumbleState[1]),
                                           reverseBits(encodedRumbleState[0]) };

    switch(rumbleStateType) {
    case RumbleStateTypeX0SingleWaveWithResonance: {
        const RumbleStateX0SingleWaveWithResonance *packedRumbleState = (RumbleStateX0SingleWaveWithResonance *)&rumbleStateFlipped;

#if RUMBLE_INCLUDE_FREQUENCY
        if(packedRumbleState->highLowSelect == 1) {
            switchRumbleStateOut->lowChannelFrequency = 160;
            switchRumbleStateOut->highChannelFrequency = frequencyFrom7BitFrequency(packedRumbleState->frequency7Bit);
        } else {
            switchRumbleStateOut->lowChannelFrequency = frequencyFrom7BitFrequency(packedRumbleState->frequency7Bit);
            switchRumbleStateOut->highChannelFrequency = 320;
        }
#endif
        if(!packedRumbleState->highChannelSwitch) {
            if(packedRumbleState->highLowSelect == 1 && packedRumbleState->frequency7Bit != 0) {
                switchRumbleStateOut->highChannelAmplitude = amplitudeFrom4BitAmplitude(packedRumbleState->highChannelAmplitude4Bit);
            }
        }
        if(!packedRumbleState->lowChannelSwitch) {
            if(packedRumbleState->highLowSelect == 0 && packedRumbleState->frequency7Bit != 0) {
                switchRumbleStateOut->lowChannelAmplitude = amplitudeFrom4BitAmplitude(packedRumbleState->lowChannelAmplitude4Bit);
            }
        }
        if(!packedRumbleState->pulse1Switch) {
            switchRumbleStateOut->pulse1Amplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse1Amplitude4Bit);
        }

        switchRumbleStateOut->pulse2Amplitude = amplitudeFrom7BitAmplitude(packedRumbleState->pulse2Amplitude7Bit);
    } break;
    case RumbleStateType0100DualWave: {
        const RumbleState0100DualWave *packedRumbleState = (RumbleState0100DualWave *)&rumbleStateFlipped;

#if RUMBLE_INCLUDE_FREQUENCY
        switchRumbleStateOut->highChannelFrequency = frequencyFrom7BitFrequency(packedRumbleState->highChannelFrequency7Bit);
#endif
        switchRumbleStateOut->highChannelAmplitude = amplitudeFrom7BitAmplitude(packedRumbleState->highChannelAmplitude7Bit);

#if RUMBLE_INCLUDE_FREQUENCY
        switchRumbleStateOut->lowChannelFrequency = frequencyFrom7BitFrequency(packedRumbleState->lowChannelFrequency7Bit);
#endif
        switchRumbleStateOut->lowChannelAmplitude = amplitudeFrom7BitAmplitude(packedRumbleState->lowChannelAmplitude7Bit);
    } break;
    case RumbleStateType0110DualResonanceWith3Pulse: {
        const RumbleState0110DualResonanceWith3Pulse *packedRumbleState = (RumbleState0110DualResonanceWith3Pulse *)&rumbleStateFlipped;

        if(!packedRumbleState->highChannelSwitch) {
#if RUMBLE_INCLUDE_FREQUENCY
            switchRumbleStateOut->highChannelFrequency = 320;
#endif
            switchRumbleStateOut->highChannelAmplitude = amplitudeFrom4BitAmplitude(packedRumbleState->highChannelAmplitude4Bit);
        }
        if(!packedRumbleState->lowChannelSwitch) {
#if RUMBLE_INCLUDE_FREQUENCY
            switchRumbleStateOut->lowChannelFrequency = 160;
#endif
            switchRumbleStateOut->lowChannelAmplitude = amplitudeFrom4BitAmplitude(packedRumbleState->lowChannelAmplitude4Bit);
        }
        if(!packedRumbleState->pulse1Switch) {
            switchRumbleStateOut->pulse1Amplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse1Amplitude4Bit);
        }
        if(!packedRumbleState->pulse2Switch) {
            switchRumbleStateOut->pulse2Amplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse2Amplitude4Bit);
        }
        switchRumbleStateOut->pulse3Amplitude = amplitudeFrom7BitAmplitude(packedRumbleState->pulse3Amplitude7Bit);
    } break;
    case RumbleStateType11DualResonanceWith4Pulse: {
        const RumbleState11DualResonanceWith4Pulse *packedRumbleState = (RumbleState11DualResonanceWith4Pulse *)&rumbleStateFlipped;

        if(!packedRumbleState->highChannelSwitch) {
#if RUMBLE_INCLUDE_FREQUENCY
            switchRumbleStateOut->highChannelFrequency = 320;
#endif
            switchRumbleStateOut->highChannelAmplitude = amplitudeFrom4BitAmplitude(packedRumbleState->highChannelAmplitude4Bit);
            if(!packedRumbleState->pulse1Or400HzSwitch) {
                switchRumbleStateOut->pulse1Amplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse1Or400HzAmplitude4Bit);
            }
        } else {
            if(!packedRumbleState->pulse1Or400HzSwitch) {
#if RUMBLE_INCLUDE_FREQUENCY
                switchRumbleStateOut->highChannelFrequency = 400;
#endif
                switchRumbleStateOut->highChannelAmplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse1Or400HzAmplitude4Bit);
            }
        }
        if(!packedRumbleState->lowChannelSwitch) {
#if RUMBLE_INCLUDE_FREQUENCY
            switchRumbleStateOut->lowChannelFrequency = 160;
#endif
            switchRumbleStateOut->lowChannelAmplitude = amplitudeFrom4BitAmplitude(packedRumbleState->lowChannelAmplitude4Bit);
        }
        if(!packedRumbleState->pulse2Switch) {
            switchRumbleStateOut->pulse3Amplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse4Amplitude4Bit);
        }
        if(!packedRumbleState->pulse3Switch) {
            switchRumbleStateOut->pulse3Amplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse4Amplitude4Bit);
        }
        if(!packedRumbleState->pulse4Switch) {
            switchRumbleStateOut->pulse4Amplitude = amplitudeFrom4BitAmplitude(packedRumbleState->pulse4Amplitude4Bit);
        }
    } break;
    default:
        break;
    }
}