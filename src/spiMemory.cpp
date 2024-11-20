#include "spiMemory.h"
#include "packedStrings.h"
#include "serial.h"

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

static const PROGMEM uint8_t x603d[] = {
    // Analog stick facrtory config
    // All values are 12 bit, like the analog sticks.
    0x77, 0x77, 0x77, // x below center, y below center: 0x599
    0xB8, 0x88, 0x86, // x below center, y below center: 0x599
    0x77, 0x77, 0x77, // x below center, y below center: 0x599

    0x00, 0x08, 0x80, // x below center, y below center: 0x599
    0x77, 0x7f, 0xf7, // x below center, y below center: 0x599
    0x77, 0x7f, 0xf7, // x below center, y below center: 0x599

    0x00,

    // 0x6050
    // Controller color. 24-bit (3 byte) RGB colors.
    0xe0, 0xe0, 0xe0, // Body
    0x77, 0x77, 0x77, // Buttons
    0xff, 0xff, 0xff, // Left Grip
    0xff, 0xff, 0xff, // Right Grip
    0xff // Extra 0xff? Maybe it signifies whether the grips are colored?
};

static const PROGMEM uint8_t x6080[] = {
    // "Factory Sensor and Stick device parameters
    0x50, 0xfd, 0x00, 0x00, 0xc6, 0x0f, 0x0f, 0x30, 0x61, 0x96, 0x30, 0xf3, 0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63
};

static const PROGMEM uint8_t x6098[] = {
    // "Factory Stick device parameters 2, normally the same as 1, even in Pro Controller"
    // [note this is indeed the same as the stick parameters above]
    0x0f, 0x30, 0x61, 0x96, 0x30, 0xf3, 0xd4, 0x14, 0x54, 0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63
};

static const PROGMEM uint8_t x8010[] = {
    // User analog stick calibration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#if 0
static const PROGMEM uint8_t x8028[] = {
    // Six axis calibration. ( Not needed because the last two bytes of the stick calibration, above, indicate no IMU calibration).
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};
#endif

static const PROGMEM spiSegment spiMemory[] = {
    { 0x6000, sizeof(x6000), x6000 },
    { 0x6020, sizeof(x6020), x6020 },
    { 0x603d, sizeof(x603d), x603d },
    { 0x6080, sizeof(x6080), x6080 },
    { 0x6098, sizeof(x6098), x6098 },
    { 0x8010, sizeof(x8010), x8010 },
    #if 0
    { 0x8028, sizeof(x8028), x8028 },
    #endif
};
static const uint8_t spiMemoryLength = sizeof(spiMemory) / sizeof(spiSegment);

bool spiMemoryRead(uint8_t *out, uint16_t address, uint16_t length)
{
    for(uint8_t i = 0; i < spiMemoryLength; ++i) {
        uint16_t segmentAddress = pgm_read_word(&spiMemory[i].address);
        if(segmentAddress <= address) {
            uint16_t segmentLength = pgm_read_word(&spiMemory[i].length);

            uint16_t segmentEnd = segmentAddress + segmentLength;
            if(segmentEnd >= address + length) {
                #if 0
                serialPrintStr6(STR6("\nSPI Read: "));
                serialPrintHex16(address);
                serialPrint(' ');
                serialPrintHex16(length);
                serialPrintStr6(STR6(" from: "));
                serialPrintHex16(segmentAddress);
                serialPrint(' ');
                serialPrintHex16(segmentLength);
                serialPrint('\n');
                #endif

                memcpy_P(out, (const uint8_t *)pgm_read_ptr(&spiMemory[i].memory) + (address - segmentAddress), length);
                return true;
            }
        }
    }

    #if 0
    serialPrintStr6(STR6("\nSPI No-Read: "));
    serialPrintHex16(address);
    serialPrint(' ');
    serialPrintHex16(length);
    serialPrint('\n');
    #endif

    return false;
}
