#include <time.h>
#include <avr/interrupt.h>
#include "common.h"

// System ticks count
volatile uint16_t ticks = 0;

//
// Timer ISR, triggers every 10ms
//
ISR(TIMER2_COMPA_vect)
{
    static uint8_t counter = 0;
    if (++counter >= 100)
    {
        counter = 0;
        system_tick();
    }
    ticks++;
}

//
// Timer tick 100/s
//
void initTimer(void)
{
    // Enable timer COMPA interrupt
    TIMSK2 |= _BV(OCIE2A);
    // Set timer compare value
    OCR2A = (F_CPU / 1024 / 100);
    // Timer mode (2) CTC
    TCCR2A = _BV(WGM21);
    // Select timer clock
    TCCR2B = _BV(CS22) | _BV(CS21) | _BV(CS20);
    sei();
}
