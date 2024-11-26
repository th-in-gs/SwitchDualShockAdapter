#include "spiMemory.h"
#include "packedStrings.h"
#include "serial.h"
#include "avr/eeprom.h"

// Defaults taken from reverse-engineering at:
// https://www.mzyy94.com/blog/2020/03/20/nintendo-switch-pro-controller-usb-gadget/
// [https://www-mzyy94-com.translate.goog/blog/2020/03/20/nintendo-switch-pro-controller-usb-gadget/?_x_tr_sl=auto&_x_tr_tl=en&_x_tr_hl=en-US&_x_tr_pto=wapp]
// https://github.com/mzyy94/nscon/blob/master/nscon.go

#define USE_PRO_CONTROLLER_DEFAULTS 0

// To save program flash space, we store only the ranges of the Pro Controller's
// SPI memory that are actually accessed by the Switch in regular usage.

struct spiSegment { uint16_t address; uint16_t length; const uint8_t *memory; };

static const PROGMEM uint8_t x6000[] = {
    // Serial number in non-extended ASCII. If first byte is >= x80, no S/N.
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

static const PROGMEM uint8_t x6020[] = {
    // Accelerometer (sixaxis IMU) factory config
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Acc XYZ origin position when completely horizontal and stick is upside
    0x00, 0x40, 0x00, 0x40, 0x00, 0x40, // Acc XYZ sensitivity special coeff, for default sensitivity: ±8G.
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Gyro XYZ origin position when still
    0x3b, 0x34, 0x3b, 0x34, 0x3b, 0x34, // Gyro XYZ sensitivity special coeff, for default sensitivity: ±2000dps
};

#if USE_PRO_CONTROLLER_DEFAULTS
static const PROGMEM uint8_t x603d[] = {
    // Analog stick factory config
    // All values are 12 bit, like the analog sticks.
    0xba, 0x15, 0x62, // 5ba, 621 [X max above, Y max above]
    0x11, 0xb8, 0x7f, // 811, 7f8 [X center, Y center]
    0x29, 0x06, 0x5b, // 629, 5b6 [X min below, Y min below]

    0xff, 0xe7, 0x7e, // 7ff, 7e7 [X center, Y center]
    0x0e, 0x36, 0x56, // 60e, 566 [X min below, Y min below]
    0x9e, 0x85, 0x60, // 59e, 605 [X max above, Y max above]

    0xff,

    // 0x6050
    // Controller color. 24-bit (3 byte) RGB colors.
    0x32, 0x32, 0x32, // Body
    0xff, 0xff, 0xff, // Buttons
    0xff, 0xff, 0xff, // Left Grip
    0xff, 0xff, 0xff, // Right Grip
    0xff, // Extra 0xff? Maybe it signifies whether the grips are colored?
};
#else
static const PROGMEM uint8_t x603d[] = {
    // Analog stick factory config
    // All values are 12 bit, like the analog sticks.
    0x77, 0x7f, 0xf7,
    0x77, 0x7f, 0xf7,
    0x77, 0x7f, 0xf7,

    0x77, 0x7f, 0xf7,
    0x77, 0x7f, 0xf7,
    0x77, 0x7f, 0xf7,

    0xff,

    // 0x6050
    // Controller color. 24-bit (3 byte) RGB colors.
    0xe0, 0xe0, 0xe0, // Body
    0x77, 0x77, 0x77, // Buttons
    0xff, 0xff, 0xff, // Left Grip
    0xff, 0xff, 0xff, // Right Grip
    0xff // Extra 0xff? Maybe it signifies whether the grips are colored?
};
#endif

#if USE_PRO_CONTROLLER_DEFAULTS
static const PROGMEM uint8_t x6080[] = {
    // "Factory Sensor and Stick device parameters
    0x50, 0xfd, 0x00, 0x00, 0xc6, 0x0f,
    0x0f, 0x30, 0x61, // Unknown
    0x96, 0x30, 0xf3, // Dead Zone: 0x096 (150), 'Range Ratio'(?): 0xf33
    0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63
};

static const PROGMEM uint8_t x6098[] = {
    // "Factory Stick device parameters 2, normally the same as 1, even in Pro Controller"
    // [note this is indeed the same as the stick parameters above]
    0x0f, 0x30, 0x61, // Unknown
    0x96, 0x30, 0xf3, // Dead Zone: 0x096 (150), 'Range Ratio'(?): 0xf33
    0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63 // Unknown
};
#else

// Dead zones manually adjusted to my PSone DualShock. Maybe wrong generally?
static const PROGMEM uint8_t x6080[] = {
    // "Factory Sensor and Stick device parameters
    0x50, 0xfd, 0x00, 0x00, 0xc6, 0x0f,
    0x0f, 0x30, 0x61, // Unknown
    0xf0, 0x30, 0xf3, // Dead Zone: 0x0f0 (240), 'Range Ratio'(?): 0xf33
    0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63
};

static const PROGMEM uint8_t x6098[] = {
    // "Factory Stick device parameters 2, normally the same as 1, even in Pro Controller"
    // [note this is indeed the same as the stick parameters above]
    0x0f, 0x30, 0x61, // Unknown
    0xf0, 0x30, 0xf3, // Dead Zone: 0x0f0 (240), 'Range Ratio'(?): 0xf33
    0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63 // Unknown
};
#endif

static const PROGMEM spiSegment spiMemory[] = {
    { 0x6000, sizeof(x6000), x6000 },
    { 0x6020, sizeof(x6020), x6020 },
    { 0x603d, sizeof(x603d), x603d },
    { 0x6080, sizeof(x6080), x6080 },
    { 0x6098, sizeof(x6098), x6098 },
};
static const uint8_t spiMemoryLength = sizeof(spiMemory) / sizeof(spiSegment);

bool spiMemoryRead(uint8_t *out, uint16_t address, uint16_t length)
{
    uint16_t afterReadAddress = address + length;

    for(uint8_t i = 0; i < spiMemoryLength; ++i) {
        uint16_t segmentAddress = pgm_read_word(&spiMemory[i].address);
        if(segmentAddress <= address) {
            uint16_t segmentLength = pgm_read_word(&spiMemory[i].length);

            uint16_t afterSegmentEnd = segmentAddress + segmentLength;
            if(afterSegmentEnd >= afterReadAddress) {
                memcpy_P(out, (const uint8_t *)pgm_read_ptr(&spiMemory[i].memory) + (address - segmentAddress), length);
                return true;
            }
        }
    }

    if(address >= 0x8010 && afterReadAddress < 0x804c) {
        // This range stores the user calibration data for the controller.
        // We store this in the ATmega's EEPROM.
        const void *eepromAddress = (const void *)(intptr_t)(address - 0x8010);
        eeprom_read_block(out, eepromAddress, length);

        debugPrintStr6(STR6("\n< SPI:\n"));
        debugPrintBuffer(out, length);

        return true;
    }
    return false;
}

bool spiMemoryWrite(uint16_t address, const uint8_t *buffer, uint16_t length)
{
    // This range stores the user calibration data for the controller.
    // We store this in the ATmega's EEPROM.
    if(address >= 0x8010 && address + length < 0x804c) {
        void *eepromAddress = (void *)(intptr_t)(address - 0x8010);
        eeprom_update_block(buffer, eepromAddress, length);

        debugPrintStr6(STR6("> SPI:\n"));
        debugPrintBuffer(buffer, length);

        return true;
    }
    return false;
}
