#include <stdio.h>
#include <avr/io.h>

int uart_putchar(char c, FILE *stream)
{
    if (c == '\n')
        uart_putchar('\r', stream);

    while (!(UCSR0A & _BV(UDRE0)))
        ; /* Wait for empty transmit buffer*/
    UDR0 = c;

    return (c);
}

int uart_getchar(FILE *stream)
{
    char c;
    while (!(UCSR0A & _BV(RXC0)))
        ;
    c = UDR0;

    if (c == '\r')
        c = '\n';

    // Echo characters
    uart_putchar(c, stdout);

    return (c);
}

// Setup the output stream for stdio
static FILE mystdout = FDEV_SETUP_STREAM(uart_putchar, NULL, _FDEV_SETUP_WRITE);
static FILE mystdin = FDEV_SETUP_STREAM(NULL, uart_getchar, _FDEV_SETUP_READ);

void initUART(void)
{
    stdout = stderr = &mystdout;
    stdin = &mystdin;
#undef BAUD // avoid compiler warning
#define BAUD 921600
#define USE_2X 0
#include <util/setbaud.h>
    UBRR0H = UBRRH_VALUE;
    UBRR0L = UBRRL_VALUE;
#if USE_2X
    UCSR0A |= (1 << U2X0);
#else
    UCSR0A &= ~(1 << U2X0);
#endif
    UCSR0C = 0x06; /* Set frame format: 8data, 1stop bit  */
    UCSR0B = _BV(TXEN0) | _BV(RXEN0);
}
