#include <stdint.h>

// This is a interrupt-free simple interface for serial output. It buffers the characters sent to it.
// It is important that`serialpoll()` be called regularly - it is what actually outputs the characters..

void serialInit(const uint32_t baudRate);
void serialPoll();

// These routines usually buffer the output.
// If  wait' is  true, they will loop calling `serialPoll()` wait until the character is being output before returning.
// Actual _transmission_ of the character by the UART hardware will occur after the return!
void serialPrint(const uint8_t ch, const bool wait = false);
//void serialPrint(const char *string, const bool wait = false);
void serialPrintStr6(const uint8_t *str6, const bool wait = false);
void serialPrintHex(uint8_t byte, const bool wait = false);
void serialPrintDec(const uint8_t ch, const bool wait = false);
