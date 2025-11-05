#include <stdio.h>
#include <stdint.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>

// Put printf strings in program memory (flash)
#define printf(str, ...) printf_P(PSTR(str), ##__VA_ARGS__)

#define RCLK_PIN PC0
#define _CE_PIN PC1
#define _RD_PIN PC2
#define _WR_PIN PC3

extern uint32_t XMODEM_ReceiveFile(int8_t *pBuffer, void (*processBlock)(int8_t *, uint16_t));
extern uint32_t XMODEM_SendFile(int8_t *pBuffer, uint32_t length, void (*processBlock)(int8_t *, uint32_t, uint16_t));
extern void initUART(void);
extern void initTimer(void);
extern void updateCRC32(uint32_t *crc, const uint8_t data);

// XMODEM receive buffer
static int8_t buffer[1024];
// Address pointer for flash programming
static uint32_t flashAddress;
// Flash size in bytes
static uint32_t flashSize = 0;
// CRC32 of flash
static uint32_t flashCRC32;
static uint32_t progCRC32;

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

static void set_data_pins_output(void)
{
    // D2-D7: PD2-PD7 (6 bits)
    DDRD |= 0b11111100; // Set PD2-PD7 as output
    // D8-D9: PB0-PB1
    DDRB |= 0b00000011; // Set PB0, PB1 as output
}

static void set_data_pins_input(void)
{
    // D2-D7: PD2-PD7
    DDRD &= ~(0b11111100); // Set PD2-PD7 as input
    // D8-D9: PB0-PB1
    DDRB &= ~(0b00000011); // Set PB0, PB1 as input
}

static void disable_data_pins_pullups(void)
{
    // D2-D7: PD2-PD7
    PORTD &= ~(0b11111100); // Disable pull-ups on PD2-PD7
    // D8-D9: PB0-PB1
    PORTB &= ~(0b00000011); // Disable pull-ups on PB0, PB1
}

// Read the 8-bit data from the data pins D2-D9
static uint8_t read_data_pins(void)
{
    return (PIND & 0b11111100) | (PINB & 0b00000011);
}

// Toggle the RCLK input on the 74hc595 to latch data
static void toggleRCLK(void)
{
    PORTC |= _BV(RCLK_PIN);  // Set RCLK high
    PORTC &= ~_BV(RCLK_PIN); // Set RCLK low
}

// Send a 16-bit address via SPI to the shift registers
static void SPI_sendAddress(uint16_t address)
{
    SPI_send((address >> 8) & 0xFF); // Send high byte
    SPI_send(address & 0xFF);        // Send low byte
    toggleRCLK();
}

static void writeCartByte(uint32_t address, uint8_t data)
{
    // Set data pins as output
    disable_data_pins_pullups();
    set_data_pins_output();

    // Send address
    SPI_sendAddress(address);
    // _CE low
    PORTC &= ~(_BV(_CE_PIN));

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
    disable_data_pins_pullups();
}

// Helper: select bank/slot derived from a full flash address and return offset
static void selectBankSlot(uint32_t address, uint16_t *offset)
{
    *offset = (address & 0x3FFF) | 0x8000;
    uint8_t bank = (address >> 14) & 0x07;
    uint8_t slot = (address >> 17) & 0x03;

    // Set bank and slot
    writeCartByte(0xffff, bank);
    writeCartByte(0xfffe, slot);
}

static uint8_t readCartByte(uint32_t address)
{
    uint8_t data;
    uint16_t offset;

    // Select bank/slot and compute offset
    selectBankSlot(address, &offset);

    // Set data pins to input
    set_data_pins_input();
    disable_data_pins_pullups();

    // Send address
    SPI_sendAddress(offset);
    // _CE low, _RD low
    PORTC &= ~(_BV(_CE_PIN) | _BV(_RD_PIN));

    // Read data from data pins
    // Dummy read to allow data to stabilize
    data = read_data_pins();
    data = read_data_pins();

    // _CE high, _RD high
    PORTC |= _BV(_RD_PIN) | _BV(_CE_PIN);

    return data;
}

// Program a byte to the flash at the specified address
//
// Programming is always performed by selecting the appropriate bank and slot
// presented at offset 0x8000.
static void progCartByte(uint32_t address, uint8_t data)
{

    uint16_t offset;

    // Select bank/slot and compute offset
    selectBankSlot(address, &offset);

    writeCartByte(0x5555, 0xaa); // Unlock command
    writeCartByte(0x2aaa, 0x55); // Unlock command
    writeCartByte(0x5555, 0xa0); // Write command
    writeCartByte(offset, data); // Write data byte
    _delay_us(10);
}

static void processBlock(int8_t *block, uint16_t length)
{
    // process the received block (e.g., write to flash)
    // Packets are always 128 bytes long for XMODEM
    for (int i = 0; i < length; i++)
    {
        progCartByte(flashAddress, block[i]);
        updateCRC32(&progCRC32, block[i]);
        flashAddress++;
    }
}

static void getFlashID(void)
{
    uint8_t manufacturerID, deviceID;

    // Send command to read ID
    writeCartByte(0x5555, 0xaa); // Unlock command
    writeCartByte(0x2aaa, 0x55); // Unlock command
    writeCartByte(0x5555, 0x90); // Read ID command

    // Read Manufacturer ID
    manufacturerID = readCartByte(0x0000);
    // Read Device ID
    deviceID = readCartByte(0x0001);

    // Exit ID mode
    writeCartByte(0x0000, 0xF0);

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
}

static void eraseFlash(void)
{
    printf("\nErasing flash...\n");
    writeCartByte(0x5555, 0xaa); // Unlock command
    writeCartByte(0x2aaa, 0x55); // Unlock command
    writeCartByte(0x5555, 0x80); // Erase command
    writeCartByte(0x5555, 0xaa); // Unlock command
    writeCartByte(0x2aaa, 0x55); // Unlock command
    writeCartByte(0x5555, 0x10); // Chip erase command
    _delay_ms(100);              // Wait for erase to complete
    printf("\nErase complete.\n");
}

static void xmodemProgramFlash(void)
{
    printf("\nStarting XMODEM file receive for programming...\n");
    // Program Flash
    flashAddress = 0;
    progCRC32 = 0xFFFFFFFF;
    XMODEM_ReceiveFile(buffer, processBlock);
    printf("\nProgramming complete. Programmed CRC32: 0x%08lX\n", progCRC32);
}

static void processSendBlock(int8_t *block, uint32_t start, uint16_t length)
{
    // process the received block (e.g., write to flash)
    // Packets are always 128 bytes long for XMODEM
    for (uint16_t i = 0; i < length; i++)
    {
        block[i] = readCartByte(start + i);
        updateCRC32(&flashCRC32, block[i]);
    }
}

static void xmodemReadFlash(void)
{
    uint32_t bytesSent;

    flashCRC32 = 0xFFFFFFFF;
    printf("\nStarting XMODEM file send for flash read...\n");
    bytesSent = XMODEM_SendFile(buffer, flashSize, processSendBlock);
    printf("Read complete (%lu bytes sent). CRC32: 0x%08lX\n", bytesSent, flashCRC32);
}

static void checksumFlash(void)
{
    printf("\nChecksuming flash...\n");
    flashCRC32 = 0xFFFFFFFF;
    for (flashAddress = 0; flashAddress < flashSize; flashAddress++)
    {
        uint8_t data = readCartByte(flashAddress);
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
}

int main(void)
{
    uint8_t input;

    // Configure control pins as output and set them high
    DDRC = _BV(RCLK_PIN) | _BV(_CE_PIN) | _BV(_RD_PIN) | _BV(_WR_PIN); // Set RCLK_PIN, _CE_PIN, _RD_PIN, _WR_PIN as output
    PORTC = _BV(_CE_PIN) | _BV(_RD_PIN) | _BV(_WR_PIN);                // Set _CE_PIN, _RD_PIN, _WR_PIN high

    initUART();
    initTimer();
    SPI_initMaster();
    set_data_pins_input();
    disable_data_pins_pullups();

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
        printf("3 ........ Program Flash (XMODEM download)\n");
        printf("4 ........ Checksum Flash\n");
        printf("5 ........ Read Byte\n");
        printf("6 ........ Program Byte\n");
        printf("7 ........ Read Flash (XMODEM upload)\n");
        printf("0 ........ Erase, Program, and Verify Flash (XMODEM download)\n");
        printf("Select an option: ");

        input = getchar();

        switch (input)
        {
        case '1':
            eraseFlash();
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '2':
            // Blank Check Flash
            printf("\nPerforming blank check...\n");
            for (flashAddress = 0; flashAddress < flashSize; flashAddress++)
            {
                if (readCartByte(flashAddress) != 0xFF)
                {
                    printf("\nFlash is NOT blank. First non-blank byte at address 0x%06lX: 0x%02X\n", flashAddress, readCartByte(flashAddress));
                    break;
                }
            }
            if (flashAddress == flashSize)
            {
                printf("\nBlank check successful.\n");
            }
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '3':
            xmodemProgramFlash();
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '4':
            // Checksum Flash
            checksumFlash();
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '5':
        {
            uint32_t address;
            printf("\nEnter address to read (hex): 0x");
            scanf("%lx", &address);
            uint8_t data = readCartByte(address);
            printf("Data at address 0x%06lX: 0x%02X\n", address, data);
            printf("Press any key to continue...\n");
            getchar(); // Consume newline
            getchar(); // Wait for key
        }
        break;
        case '6':
        {
            uint32_t address;
            uint8_t data;
            printf("\nEnter address to write (hex): 0x");
            scanf("%lx", &address);
            printf("Enter data to write (hex): 0x");
            scanf("%hhx", &data);
            progCartByte(address, data);
            printf("Press any key to continue...\n");
            getchar(); // Consume newline
            getchar(); // Wait for key
        }
        break;
        case '7':
            // Read Flash via XMODEM
            xmodemReadFlash();
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '0':
            eraseFlash();
            xmodemProgramFlash();
            checksumFlash();
            printf("Press any key to continue...\n");
            getchar();
            break;
        default:
            break;
        }
    }
}
