#include <serial.h>
#include <avr/io.h>

// A simple circular buffer to store serial output.
static const uint8_t sSerialOutputBufferLength = 128;
static uint8_t sSerialOutputBuffer[sSerialOutputBufferLength];
static uint8_t sSerialOutputBufferStart = 0;
static uint8_t sSerialOutputBufferEnd = 0;

void serialInit(const uint32_t baudRate)
{
    const uint16_t ubrr = (F_CPU + (baudRate * 4)) / (baudRate * 16) - 1;

    UBRRH = (uint8_t)(ubrr >> 8);
    UBRRL = (uint8_t)ubrr;

    UCSRB = (1 << TXEN);
    UCSRC = (1 << URSEL) | (1 << UCSZ1) | (1 << UCSZ0);
}

void serialPoll()
{
    if(sSerialOutputBufferStart != sSerialOutputBufferEnd) {
        if(UCSRA & (1 << UDRE)) {
            UDR = sSerialOutputBuffer[sSerialOutputBufferStart];
            sSerialOutputBufferStart = (sSerialOutputBufferStart + 1) % sSerialOutputBufferLength;
        }
    }
}

void serialPrint(const uint8_t ch, const bool wait)
{
    if(wait) {
        while(sSerialOutputBufferStart != sSerialOutputBufferEnd) {
            serialPoll();
        }
    }
    sSerialOutputBuffer[sSerialOutputBufferEnd] = ch;
    sSerialOutputBufferEnd = (sSerialOutputBufferEnd + 1) % sSerialOutputBufferLength;
}

static void serialPrintNybble(const uint8_t nybble, const bool wait)
{
    serialPrint(nybble >= 0xA ? 'A' - 0xA + nybble : '0' + nybble, wait);
}

void serialPrintHex(uint8_t byte, const bool wait)
{
    serialPrintNybble(byte >> 4, wait);
    serialPrintNybble(byte & 0xF, wait);
}

void serialPrintDec(const uint8_t ch, const bool wait)
{
    uint8_t toPrint = ch % 10;
    uint8_t remaining = ch / 10;
    if(remaining) {
        serialPrintDec(remaining, wait);
    }
    serialPrint('0' + toPrint);
}

void serialPrint(const char *string, const bool wait)
{
    uint8_t ch;
    do {
        ch = *string;
        if(ch) {
            serialPrint(ch, wait);
            ++string;
        }
    } while(ch != 0);
}
