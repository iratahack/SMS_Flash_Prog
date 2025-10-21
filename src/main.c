#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <time.h>

#define RCLK_PIN PC0
#define _CE_PIN PC1
#define _RD_PIN PC2
#define _WR_PIN PC3

extern uint32_t XMODEM_ReceiveFile(int8_t *pBuffer);
extern void initUART(void);
extern void initTimer(void);

static int8_t buffer[256]; // 256B buffer for XMODEM transfer

void SPI_initMaster(void)
{
    // Set MOSI (PB3), SCK (PB5), SS (PB2) as output
    DDRB |= (1 << PB3) | (1 << PB5) | (1 << PB2);
    // Set MISO (PB4) as input
    DDRB &= ~(1 << PB4);

    // Enable SPI, Set as Master, Set clock rate fosc/4
    SPCR = (1 << SPE) | (1 << MSTR);
    // Double speed for fosc/2
    SPSR |= (1 << SPI2X);
}

void SPI_send(uint8_t data)
{
    SPDR = data; // Load data into the buffer
    while (!(SPSR & (1 << SPIF)))
        ; // Wait until transmission complete
}

void set_data_pins_output(void)
{
    // D2-D7: PD2-PD7 (6 bits)
    DDRD |= 0b11111100; // Set PD2-PD7 as output
    // D8-D9: PB0-PB1
    DDRB |= 0b00000011; // Set PB0, PB1 as output
}

void set_data_pins_input(void)
{
    // D2-D7: PD2-PD7
    DDRD &= ~(0b11111100); // Set PD2-PD7 as input
    // D8-D9: PB0-PB1
    DDRB &= ~(0b00000011); // Set PB0, PB1 as input
}

void enable_data_pins_pullups(void)
{
    // D2-D7: PD2-PD7
    PORTD |= 0b11111100; // Enable pull-ups on PD2-PD7
    // D8-D9: PB0-PB1
    PORTB |= 0b00000011; // Enable pull-ups on PB0, PB1
}

void disable_data_pins_pullups(void)
{
    // D2-D7: PD2-PD7
    PORTD &= ~(0b11111100); // Disable pull-ups on PD2-PD7
    // D8-D9: PB0-PB1
    PORTB &= ~(0b00000011); // Disable pull-ups on PB0, PB1
}

// Read the 8-bit data from the data pins D2-D9
uint8_t read_data_pins(void)
{
    return (PORTD & 0b11111100) | (PORTB & 0b00000011);
}

// Toggle the RCLK input on the 74hc595 to latch data
void toggleRCLK(void)
{
    PORTC |= (1 << RCLK_PIN);  // Set RCLK high
    PORTC &= ~(1 << RCLK_PIN); // Set RCLK low
}

// Send a 16-bit address via SPI to the shift registers
void SPI_sendAddress(uint16_t address)
{
    SPI_send((address >> 8) & 0xFF); // Send high byte
    SPI_send(address & 0xFF);        // Send low byte
    toggleRCLK();
}

int main(void)
{
    uint8_t input;

    // Configure control pins as output and set them high
    DDRC = (1 << RCLK_PIN) | (1 << _CE_PIN) | (1 << _RD_PIN) | (1 << _WR_PIN);  // Set RCLK_PIN, _CE_PIN, _RD_PIN, _WR_PIN as output
    PORTC = (1 << _CE_PIN) | (1 << _RD_PIN) | (1 << _WR_PIN); // Set _CE_PIN, _RD_PIN, _WR_PIN high

    initUART();
//    initTimer();
    SPI_initMaster();
    set_data_pins_input();
    enable_data_pins_pullups();

    for (;;)
    {
        static uint16_t toggle = 0;

        printf("\033[2J\033[H"); // Clear terminal
        printf("SMS Flash Programmer Initialized\n\n");
        printf("1 ........ Erase Flash\n");
        printf("2 ........ Verify Flash\n");
        printf("3 ........ Program Flash\n");
        printf("4 ........ Receive File\n");

        input = getchar();

        switch (input)
        {
            case '1':
                // Erase Flash
                break;
            case '2':
                // Verify Flash
                break;
            case '3':
                // Program Flash
                break;
            case '4':
                // Receive File
                memset(buffer, 0, sizeof(buffer));
                XMODEM_ReceiveFile(buffer);
                break;
            default:
                break;
        }

        SPI_sendAddress(toggle);
        toggle ^= 1;
    }
}
