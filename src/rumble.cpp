// Rumble translation with help from MissionControl:
// https://github.com/ndeadly/MissionControl/blob/master/mc_mitm/source/controllers/emulated_switch_controller.cpp
//
// Which in turn got them from dekunukem repo normalised and scaled by function used by yuzu
// https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/rumble_data_table.md#amplitude-table
// https://github.com/yuzu-emu/yuzu/blob/d3a4a192fe26e251f521f0311b2d712f5db9918e/src/input_common/sdl/sdl_impl.cpp#L429

#include "rumble.h"
#include <avr/pgmspace.h>
#include <string.h>
#include <math.h>


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

static constexpr uint8_t floatToRumble(double f)
{
    // Map 0.0f - 1.0f to the range 0x40 - 0xff - these are the values
    // for which the dual shock motor will spin.
    return f == 0.0 ? 0.0 : (uint8_t)round(0x40 + ((0xff - 0x40) * f));
}

static const PROGMEM uint8_t rumble_amp_lut[] = {
    floatToRumble(0.000000),
    floatToRumble(0.120576),
    floatToRumble(0.137846),
    floatToRumble(0.146006),
    floatToRumble(0.154745),
    floatToRumble(0.164139),
    floatToRumble(0.174246),
    floatToRumble(0.185147),
    floatToRumble(0.196927),
    floatToRumble(0.209703),
    floatToRumble(0.223587),
    floatToRumble(0.238723),
    floatToRumble(0.255268),
    floatToRumble(0.273420),
    floatToRumble(0.293398),
    floatToRumble(0.315462),
    floatToRumble(0.321338),
    floatToRumble(0.327367),
    floatToRumble(0.333557),
    floatToRumble(0.339913),
    floatToRumble(0.346441),
    floatToRumble(0.353145),
    floatToRumble(0.360034),
    floatToRumble(0.367112),
    floatToRumble(0.374389),
    floatToRumble(0.381870),
    floatToRumble(0.389564),
    floatToRumble(0.397476),
    floatToRumble(0.405618),
    floatToRumble(0.413996),
    floatToRumble(0.422620),
    floatToRumble(0.431501),
    floatToRumble(0.436038),
    floatToRumble(0.440644),
    floatToRumble(0.445318),
    floatToRumble(0.450062),
    floatToRumble(0.454875),
    floatToRumble(0.459764),
    floatToRumble(0.464726),
    floatToRumble(0.469763),
    floatToRumble(0.474876),
    floatToRumble(0.480068),
    floatToRumble(0.485342),
    floatToRumble(0.490694),
    floatToRumble(0.496130),
    floatToRumble(0.501649),
    floatToRumble(0.507256),
    floatToRumble(0.512950),
    floatToRumble(0.518734),
    floatToRumble(0.524609),
    floatToRumble(0.530577),
    floatToRumble(0.536639),
    floatToRumble(0.542797),
    floatToRumble(0.549055),
    floatToRumble(0.555413),
    floatToRumble(0.561872),
    floatToRumble(0.568436),
    floatToRumble(0.575106),
    floatToRumble(0.581886),
    floatToRumble(0.588775),
    floatToRumble(0.595776),
    floatToRumble(0.602892),
    floatToRumble(0.610127),
    floatToRumble(0.617482),
    floatToRumble(0.624957),
    floatToRumble(0.632556),
    floatToRumble(0.640283),
    floatToRumble(0.648139),
    floatToRumble(0.656126),
    floatToRumble(0.664248),
    floatToRumble(0.672507),
    floatToRumble(0.680906),
    floatToRumble(0.689447),
    floatToRumble(0.698135),
    floatToRumble(0.706971),
    floatToRumble(0.715957),
    floatToRumble(0.725098),
    floatToRumble(0.734398),
    floatToRumble(0.743857),
    floatToRumble(0.753481),
    floatToRumble(0.763273),
    floatToRumble(0.773235),
    floatToRumble(0.783370),
    floatToRumble(0.793684),
    floatToRumble(0.804178),
    floatToRumble(0.814858),
    floatToRumble(0.825726),
    floatToRumble(0.836787),
    floatToRumble(0.848044),
    floatToRumble(0.859502),
    floatToRumble(0.871165),
    floatToRumble(0.883035),
    floatToRumble(0.895119),
    floatToRumble(0.907420),
    floatToRumble(0.919943),
    floatToRumble(0.932693),
    floatToRumble(0.945673),
    floatToRumble(0.958889),
    floatToRumble(0.972345),
    floatToRumble(0.986048),
    floatToRumble(1.000000)
};

enum RumbleStateType {
    RumbleStateTypeX0SingleWaveWithResonance,
    RumbleStateType0100DualWave,
    RumbleStateType0101Silent,
    RumbleStateType0110DualResonanceWith3Pulse,
    RumbleStateType11DualResonanceWith4Pulse,
    RumbleStateTypeUnrecognized = 0xff
};

RumbleStateType rumbleStateTypeFromRumbleState(const uint8_t *rumbleState)
{
    switch((rumbleState[3] & 0b11000000) >> 4) {
    case 0b00:
    case 0b10:
        return RumbleStateTypeX0SingleWaveWithResonance;
    case 0b01:
        switch(rumbleState[1] & 0b00000011) {
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

struct DecodedRumbleState {
    uint16_t lowChannelFrequency;
    uint8_t lowChannelAmplitude;
    uint16_t highChannelFrequency;
    uint8_t highChannelAmplitude;
    uint8_t pulse1Amplitude;
    bool pulse1switch;
    uint8_t pulse2Amplitude;
    bool pulse2switch;
    uint8_t pulse3Amplitude;
    bool pulse3switch;
    uint8_t pulse4Amplitude;
    bool pulse4switch;
    uint8_t hz400Amplitude;
};

void decodeRumble(const uint8_t *rumbleState)
{
    DecodedRumbleState decodedRumbleState;
    DecodedRumbleState *pDecodedRumbleState = &decodedRumbleState;

    memset(pDecodedRumbleState, 0, sizeof(DecodedRumbleState));

    switch(rumbleStateTypeFromRumbleState(rumbleState)) {
    case RumbleStateTypeX0SingleWaveWithResonance: {
        bool isHighFrequency = (rumbleState[0] & 1) ? true : false;
        uint8_t frequency4Bit = rumbleState[0] >> 1;

        uint8_t highFreqAmplitude4Bit = rumbleState[1] & 0b00001111;
        bool highFreqSwitch = (rumbleState[1] & 0b00010000) ? false : true;

        uint8_t lowFreqAmplitude4Bit = (rumbleState[1] >> 5) | ((rumbleState[2] & 0x01) << 3);
        bool lowFreqSwitch = (rumbleState[2] & 0b00000010) ? false : true;

        uint8_t pulse1Amplitude4Bit = (rumbleState[2] >> 1) & 0x00001111;
        bool pulse1Switch = (rumbleState[2] & 0b01000000) ? false : true;

        uint8_t pulse2Amplitude7Bit = (rumbleState[3] & 0x00111111) << 1 | (rumbleState[2] >> 7);
    } break;
    case RumbleStateType0100DualWave: {
        uint8_t highFreqFrequency7Bit = (rumbleState[0] >> 2) | ((rumbleState[1] & 1) << 6);
        uint8_t highFreqAmplitude7Bit = rumbleState[1] >> 1;

        uint8_t lowFreqFrequeny7Bit = rumbleState[2] & 0b01111111;
        uint8_t lowFreqAmplitude7Bit = (rumbleState[2] >> 7) & ((rumbleState[2] & 0b00111111) << 1);
    } break;
    case RumbleStateType0110DualResonanceWith3Pulse: {
        uint8_t highFreqAmplitude4Bit = (rumbleState[0] & 0b01111000) >> 3;
        bool highFreqSwitch = (rumbleState[0] & 0b10000000) ? false : true;

        uint8_t lowFreqAmplitude4Bit = (rumbleState[1] & 0b00001111);
        bool lowFreqSwitch = (rumbleState[1] & 0b00010000) ? false : true;

        uint8_t pulse1Amplitude4Bit = (rumbleState[1] >> 5) & ((rumbleState[2] & 1) << 3);
        bool pulse1Switch = (rumbleState[2] & 0b00000010) ? false : true;

        uint8_t pulse2Amplitude4Bit = (rumbleState[2] >> 2) & 0b00001111;
        bool pulse2Switch = (rumbleState[2] & 0b01000000) ? false : true;

        uint8_t pulse3Amplitude7Bit = (rumbleState[3] & 0b01111111);
    } break;
    case RumbleStateType11DualResonanceWith4Pulse: {
        uint8_t highFreqAmplitude4Bit = rumbleState[0] & 0b00001111;
        bool highFreqSwitch = (rumbleState[0] & 0b00010000) ? false : true;

        uint8_t lowFreqAmplitude4Bit = (rumbleState[0] >> 5) | ((rumbleState[1] & 0x01) << 3);
        bool lowFreqSwitch = (rumbleState[1] & 0b00000010) ? false : true;

        uint8_t pulse1OrHz400Amplitude4Bit = (rumbleState[1] >> 2) & 0x00001111;
        bool pulse1OrHz400Switch = (rumbleState[1] & 0b01000000) ? false : true;

        uint8_t pulse2Amplitude4Bit = (rumbleState[1] >> 7) & ((rumbleState[2] & 0x111) << 1);
        bool pulse2Switch = (rumbleState[2] & 0b00001000) ? false : true;

        uint8_t pulse3Amplitude4Bit = rumbleState[2] >> 4;
        bool pulse3Switch = (rumbleState[3] & 1) ? false : true;

        uint8_t pulse4Amplitude4Bit = ((rumbleState[3] >> 1) & 0b00001111);
        bool pulse4Switch = (rumbleState[3] & 0b00100000) ? false : true;
    } break;
    case RumbleStateType0101Silent:
    default:
        break;
    }
}

uint8_t amplitudeFromRumbleState(const uint8_t *rumbleState)
{
    uint8_t hi_amp_ind = rumbleState[1] >> 1;
    uint8_t lo_amp_ind = ((rumbleState[3] - 0x40) << 1) + (rumbleState[2] >> 7);

    // Kind of weirdly, sometimes the switch seems to send 'too big' values to
    // mean zero?
    if(hi_amp_ind >= sizeof(rumble_amp_lut)) {
        hi_amp_ind = 0;
    }
    if(lo_amp_ind >= sizeof(rumble_amp_lut)) {
        lo_amp_ind = 0;
    }

    return pgm_read_byte(&rumble_amp_lut[hi_amp_ind > lo_amp_ind ? hi_amp_ind : lo_amp_ind]);
}

uint16_t frequencyFromRumbleState(const uint8_t *rumbleState)
{
    uint8_t hi_freq_ind = 0x20 + (rumbleState[0] >> 2) + ((rumbleState[1] & 0x01) * 0x40) - 1;
    uint8_t lo_freq_ind = (rumbleState[2] & 0x7f) - 1;

    if(hi_freq_ind >= sizeof(rumble_freq_lut)) {
        hi_freq_ind = 0;
    }
    if(lo_freq_ind >= sizeof(rumble_freq_lut)) {
        lo_freq_ind = 0;
    }

    return pgm_read_word(&rumble_freq_lut[hi_freq_ind > lo_freq_ind ? hi_freq_ind : lo_freq_ind]);
}