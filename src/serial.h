#ifndef __timer_h_included__
#define __timer_h_included__

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
void serialPrintHex16(uint16_t byte, const bool wait = false);
void serialPrintDec(const uint8_t ch, const bool wait = false);
void serialPrintBuffer(const void *buffer, uint8_t length);


#ifndef DEBUG_PRINT_ON
#define DEBUG_PRINT_ON 1
#endif

#if DEBUG_PRINT_ON
#define debugPrint(...) serialPrint(__VA_ARGS__)
#define debugPrintStr6(...) serialPrintStr6(__VA_ARGS__)
#define debugPrintHex(...) serialPrintHex(__VA_ARGS__)
#define debugPrintHex16(...) serialPrintHex16(__VA_ARGS__)
#define debugPrintDec(...) serialPrintDec(__VA_ARGS__)
#define debugPrintBuffer(...) serialPrintBuffer(__VA_ARGS__)
#else
#define debugPrint(...)
#define debugPrintStr6(...)
#define debugPrintHex(...)
#define debugPrintHex16(...)
#define debugPrintDec(...)
#define debugPrintBuffer(...)
#endif

#endif // __timer_h_included__
