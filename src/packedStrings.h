#ifndef __packedstrings_h_included__
#define __packedstrings_h_included__

#include <stdint.h>
#include <string.h>
#include <avr/pgmspace.h>

#define STR7(str) (__extension__({ static PROGMEM const PackedStringConstexpr<7, sizeof(str)> __c = PackedStringConstexpr<7, sizeof(str)>(str); __c.data; }))
#define STR6(str) (__extension__({ static PROGMEM const PackedStringConstexpr<6, sizeof(str)> __c = PackedStringConstexpr<6, sizeof(str)>(str); __c.data; }))

const uint8_t str7CharAtIndex(const uint8_t *data, const uint8_t index);
const uint8_t str6CharAtIndex(const uint8_t *data, const uint8_t index);


template <size_t bitsPerChar, size_t length>
struct PackedStringConstexpr {
    uint8_t data[((length * bitsPerChar) / 8) + (((length * bitsPerChar) % 8) ? 1 : 0)];
    constexpr PackedStringConstexpr(const char (&string)[length]) : data {}
    {
        size_t outBytePosition = 0;
        size_t outBitPosition = 0;
        for(size_t inBytePosition = 0; inBytePosition < length; ++inBytePosition) {
            uint8_t ch = (uint8_t)string[inBytePosition];
            if(bitsPerChar == 6) {
                // Custom encoding scheme for a 64-element subset of the ASCII
                // set.
                if(ch >= 'a' && ch <= 'z') {
                    ch = (ch - 'a') + 'A';
                }
                bool wasZero = ch == '\0';
                bool wasNewline = ch == '\n';
                bool wasPipe = ch == '|';
                if(ch < 0x20 || ch > 0x5d) {
                    ch = '?';
                }
                if(wasNewline) {
                    ch = '^';
                }
                if(wasPipe) {
                    ch = '!';
                }
                if(wasZero) {
                    ch = ';';
                }
                ch = ch - 0x20;
                ch = ch << 2;
            } else {
                ch = ch << 1;
            }
            data[outBytePosition] |= ch >> outBitPosition;
            outBitPosition += bitsPerChar;
            if(outBitPosition >= 8) {
                ++outBytePosition;
                outBitPosition -= 8;
                if(outBitPosition > 0) {
                    data[outBytePosition] |= ch << (bitsPerChar - outBitPosition);
                }
            }
        }
    }
};

#endif // __packedstrings_h_included__