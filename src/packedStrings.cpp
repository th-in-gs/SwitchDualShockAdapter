#include "packedStrings.h"

const uint8_t str7CharAtIndex(const uint8_t *data, const uint8_t index) {
    const size_t bitPosition = index * 7;
    const uint8_t startByte = bitPosition / 8;
    const uint8_t startBit = bitPosition % 8;
    uint8_t ch = pgm_read_byte(&data[startByte]) << startBit;
    if(startBit > (8-7)) {
        ch |= pgm_read_byte(&data[startByte + 1]) >> (8 - startBit);
    }
    return ch >> 1;
}

const uint8_t str6CharAtIndex(const uint8_t *data, const uint8_t index) {
    const size_t bitPosition = index * 6;
    const uint8_t startByte = bitPosition >>3;
    const uint8_t startBit = bitPosition % 8;
    uint8_t ch = pgm_read_byte(&data[startByte]) << startBit;
    if(startBit > (8-6)) {
        ch |= pgm_read_byte(&data[startByte + 1]) >> (8 - startBit);
    }
    ch = 0x20 + (ch >> 2);
    switch(ch) {
    case '^':
        ch = '\n';
        break;
    case '!':
        ch = '|';
        break;
    case ';':
        ch = '\0';
        break;
    default:
        break;
    }
    return ch;
}