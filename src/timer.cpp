#include <avr/io.h>
#include <avr/interrupt.h>

// Very simple timer routines, only able to count milliseconds in uint8_t
// (so only 0-255 millis before reset!)
// We don't need to count up higher than
// the number of timer fires it would take to get to 256ms.

void timerInit() {
    // Timer set to increment every F_CPU / 64 cycles
    // (this is necessary for the osccal.h oscilator calibration to work!)
    TCCR0 = (TCCR0 & ~0b111) | 0b011;

    // Enable the interupt.
    TIMSK |= 1 << TOIE0;
}

// (F_CPU / 64) = Timer counts per second.
// (F_CPU / 64) / 256 = overflows per second.
// ((F_CPU / 64) / 256) / 1000 = overflows per millisecond.
// ((F_CPU / 64) / 256) / 1000 * 256 = overflows per _256_ milliseconds.
//    = (F_CPU / 64) / 1000  = overflows per 256 milliseconds.
//    = F_CPU / 64000  = overflows per 256 milliseconds.
static const uint8_t sTimerZeroOverflowsPer256Millis = F_CPU / 64000;
static volatile uint8_t sTimer0FireCount = 0;

ISR(TIMER0_OVF_vect, ISR_NOBLOCK) {
    sTimer0FireCount = (uint8_t)(sTimer0FireCount + 1) % sTimerZeroOverflowsPer256Millis;
}

uint8_t timerMillis() {
    // We can't just divide by 'fires per millisecond' directly
    // because that's ((F_CPU / 64) / 256) / 1000 - which integer-rounds to zero!
    // So we multiply by 256 then divide by sTimerZeroFiresPer256Millis.
    return (uint8_t)(((uint16_t)sTimer0FireCount * 256) / (uint16_t)sTimerZeroOverflowsPer256Millis);
}