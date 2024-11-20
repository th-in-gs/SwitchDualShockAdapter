#ifndef __spimemory_h_included__
#define __spimemory_h_included__

#include <stdint.h>
#include <avr/pgmspace.h>

bool spiMemoryRead(uint8_t *out, uint16_t address, uint16_t length);

#endif