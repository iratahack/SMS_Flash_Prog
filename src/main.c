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

extern uint32_t XMODEM_ReceiveFile(int8_t *pBuffer, void (*processBlock)(int8_t *, uint16_t));
extern void initUART(void);
extern void initTimer(void);
extern void updateCRC32(uint32_t *crc, const uint8_t data);

// XMODEM receive buffer
static int8_t buffer[128];
// Address pointer for flash programming
static uint32_t flashAddress;
// Flash size in bytes
static uint32_t flashSize = 0;
// CRC32 of flash
static uint32_t flashCRC32;
static uint32_t progCRC32;

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

uint8_t readFlashByte(uint32_t address)
{
    uint8_t data;

    // Set data pins to input
    set_data_pins_input();
    enable_data_pins_pullups();

    // Send address
    SPI_sendAddress(address);
    // _CE low, _RD low
    PORTC &= ~(_BV(_CE_PIN) | _BV(_RD_PIN));

    // Read data from data pins
    data = read_data_pins();

    // _CE high, _RD high
    PORTC |= _BV(_RD_PIN) | _BV(_CE_PIN);

    return data;
}

void writeFlashByte(uint32_t address, uint8_t data)
{
    // Set data pins as output
    disable_data_pins_pullups();
    set_data_pins_output();

    // Send address
    SPI_sendAddress(address);
    // _CE low, _RD high
    PORTC = PORTC & ~(_BV(_CE_PIN)) | _BV(_RD_PIN);

    // Write data to data pins
    PORTD = (PORTD & 0b00000011) | (data & 0b11111100); // D2-D7
    PORTB = (PORTB & 0b11111100) | (data & 0b00000011); // D8-D9

    // Pulse WR to write data, min delay is 40nS
    PORTC &= ~(_BV(_WR_PIN));
    PORTC |= _BV(_WR_PIN);

    // _CE high
    PORTC |= _BV(_CE_PIN);

    // Set data pins back to input
    set_data_pins_input();
    enable_data_pins_pullups();
}

void processBlock(int8_t *block, uint16_t length)
{
    // process the received block (e.g., write to flash)
    // Packets are always 128 bytes long for XMODEM
    for (int i = 0; i < length; i++)
    {
        writeFlashByte(0x5555, 0xaa);             // Unlock command
        writeFlashByte(0x2aaa, 0x55);             // Unlock command
        writeFlashByte(0x5555, 0xa0);             // Write command
        writeFlashByte(flashAddress++, block[i]); // Write data byte
        updateCRC32(&progCRC32, block[i]);
    }
}
void getFlashID(void)
{
    uint8_t manufacturerID, deviceID;

    // Set data pins to input
    set_data_pins_input();
    enable_data_pins_pullups();

    // Send command to read ID
    writeFlashByte(0x5555, 0xaa); // Unlock command
    writeFlashByte(0x2aaa, 0x55); // Unlock command
    writeFlashByte(0x5555, 0x90); // Read ID command

    // Read Manufacturer ID
    manufacturerID = readFlashByte(0x0000);
    // Read Device ID
    deviceID = readFlashByte(0x0001);

    switch (manufacturerID)
    {
    case 0xBF:
        printf("Manufacturer: SST (MCHP)\n");
        switch (deviceID)
        {
        case 0xB5:
            printf(" (SST39SF010)\n");
            flashSize = ((uint32_t)128 * (uint32_t)1024); // 128KB
            break;
        case 0xB6:
            printf(" (SST39SF020)\n");
            flashSize = ((uint32_t)256 * (uint32_t)1024); // 256KB
            break;
        case 0xB7:
            printf(" (SST39SF040)\n");
            flashSize = ((uint32_t)512 * (uint32_t)1024); // 512KB
            break;
        default:
            printf("Device      : Unknown (0x%02X)\n", deviceID);
            break;
        }
        break;
    default:
        printf("Manufacturer: Unknown (0x%02X)\n", manufacturerID);
        printf("Device      : Unknown (0x%02X)\n", deviceID);
        break;
    }
    flashSize = ((uint32_t)512 * (uint32_t)1024); // 512KB
    // Exit ID mode
    writeFlashByte(0x0000, 0xF0); // Reset command
}

int main(void)
{
    uint8_t input;

    // Configure control pins as output and set them high
    DDRC = (1 << RCLK_PIN) | (1 << _CE_PIN) | (1 << _RD_PIN) | (1 << _WR_PIN); // Set RCLK_PIN, _CE_PIN, _RD_PIN, _WR_PIN as output
    PORTC = (1 << _CE_PIN) | (1 << _RD_PIN) | (1 << _WR_PIN);                  // Set _CE_PIN, _RD_PIN, _WR_PIN high

    initUART();
    initTimer();
    SPI_initMaster();
    set_data_pins_input();
    enable_data_pins_pullups();

    for (;;)
    {
        printf("\033[2J\033[H"); // Clear terminal
        printf("SMS Flash Programmer Initialized\n\n");
        getFlashID();
        printf("Flash Size  : %luKB\n", flashSize / 1024);
        printf("Flash CRC32 : 0x%08lX\n", flashCRC32);
        printf("Prog. CRC32 : 0x%08lX\n", progCRC32);
        printf("=====================================\n");
        printf("Menu:\n");
        printf("1 ........ Erase Flash\n");
        printf("2 ........ Blank Check Flash\n");
        printf("3 ........ Program Flash\n");
        printf("4 ........ Verify Flash\n");
        printf("Select an option: ");

        input = getchar();

        switch (input)
        {
        case '1':
            // Erase Flash
            writeFlashByte(0x5555, 0xaa); // Unlock command
            writeFlashByte(0x2aaa, 0x55); // Unlock command
            writeFlashByte(0x5555, 0x80); // Erase command
            writeFlashByte(0x5555, 0xaa); // Unlock command
            writeFlashByte(0x2aaa, 0x55); // Unlock command
            writeFlashByte(0x5555, 0x10); // Chip erase command
            printf("\nFlash Erase Command Issued.\n");
            break;
        case '2':
            // Blank Check Flash
            printf("\nPerforming Blank Check...\n");
            for (flashAddress = 0; flashAddress < flashSize; flashAddress++)
            {
                if (readFlashByte(flashAddress) != 0xFF)
                {
                    printf("\nFlash is NOT blank. First non-blank byte at address 0x%06lX: 0x%02X\n", flashAddress, readFlashByte(flashAddress));
                    break;
                }
            }
            break;
        case '3':
            // Program Flash
            flashAddress = 0;
            progCRC32 = 0xFFFFFFFF;
            XMODEM_ReceiveFile(buffer, processBlock);
            break;
        case '4':
            // Verify Flash
            printf("\nVerifying Flash...\n");
            flashCRC32 = 0xFFFFFFFF;
            for (flashAddress = 0; flashAddress < flashSize; flashAddress++)
            {
                uint8_t data = readFlashByte(flashAddress);
                updateCRC32(&flashCRC32, data);
            }
            if (flashCRC32 == progCRC32)
            {
                printf("\nFlash verification successful. CRC32 matches: 0x%08lX\n", flashCRC32);
            }
            else
            {
                printf("\nFlash verification failed. Expected CRC32: 0x%08lX, Read CRC32: 0x%08lX\n", progCRC32, flashCRC32);
            }
            printf("Press any key to continue...\n");
            getchar();
            break;
        default:
            break;
        }
    }
}
