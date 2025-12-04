#include <avr/io.h>
#include <util/delay.h>

#define RCLK_PIN PB2 // SS

static void SPI_initMaster(void)
{
    // Set MOSI (PB3), SCK (PB5), SS (PB2) as output
    DDRB |= (_BV(PB3) | _BV(PB5) | _BV(PB2));
    // Set MISO (PB4) as input
    DDRB &= ~(_BV(PB4));

    // Enable SPI, Set as Master, Set clock rate fosc/4
    SPCR = _BV(SPE) | _BV(MSTR);
    // Double speed for fosc/2
    SPSR = _BV(SPI2X);
}

static void SPI_send(uint8_t data)
{
    SPDR = data; // Load data into the buffer
    while (!(SPSR & _BV(SPIF)))
        ; // Wait until transmission complete
}

static void sendByte(uint8_t byte)
{
    SPI_send(byte);          // Send low byte
    PORTB |= _BV(RCLK_PIN);  // Set RCLK high
    PORTB &= ~_BV(RCLK_PIN); // Set RCLK low
}

int main(void)
{
    uint8_t v = 0x00;
    SPI_initMaster();

    while (1)
    {
        sendByte(v);
        v ^= 0x80;
        _delay_ms(1000);
    }
}
