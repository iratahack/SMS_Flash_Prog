#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/pgmspace.h>
#include "mappers.h"

// Put printf strings in program memory (flash)
#define printf(str, ...) printf_P(PSTR(str), ##__VA_ARGS__)

#define RCLK_PIN PC0
#define _CE_PIN PC1
#define _RD_PIN PC2
#define _WR_PIN PC3

extern uint32_t XMODEM_ReceiveFile(uint8_t *pBuffer, void (*processBlock)(uint8_t *, uint16_t));
extern uint32_t XMODEM_SendFile(uint8_t *pBuffer, uint32_t length, void (*processBlock)(uint8_t *, uint32_t, uint16_t));
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
// CRC32 of programmed data
static uint32_t progCRC32;
// Current mapper index
static uint8_t mapperIndex;
// Y position for text output
static uint8_t yPos;

/* ANSI color helpers */
#define ESC "\x1b"
#define CSI "\x1b["

#define COLOR_RESET CSI "0m"
#define COLOR_BRIGHT CSI "1m"
#define COLOR_DIM CSI "2m"

/* Foreground */
#define FG_RED CSI "31m"
#define FG_GREEN CSI "32m"
#define FG_YELLOW CSI "33m"
#define FG_BLUE CSI "34m"
#define FG_MAGENTA CSI "35m"
#define FG_CYAN CSI "36m"
#define FG_WHITE CSI "37m"

/* Background */
#define BG_BLUE CSI "44m"
#define BG_CYAN CSI "46m"

/* Box drawing - using Unicode box-drawing characters */
const char *TL = "┌";
const char *TR = "┐";
const char *BL = "└";
const char *BR = "┘";
const char *H = "─";
const char *V = "│";
const char *TH = "┬";
const char *BH = "┴";
const char *LH = "├";
const char *RH = "┤";
const char *X = "┼";

/* Helpers to move cursor and clear */
void clear_screen(void)
{
    printf(CSI "2J" CSI "H");
    fflush(stdout);
}
void move_to(int r, int c)
{
    printf(CSI "%d;%dH", r, c);
    fflush(stdout);
}
void hide_cursor(void)
{
    printf(CSI "?25l");
    fflush(stdout);
}
void show_cursor(void)
{
    printf(CSI "?25h");
    fflush(stdout);
}

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

static void disable_data_pins_pullups(void)
{
    // D2-D7: PD2-PD7
    PORTD &= ~(0b11111100); // Disable pull-ups on PD2-PD7
    // D8-D9: PB0-PB1
    PORTB &= ~(0b00000011); // Disable pull-ups on PB0, PB1
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
    disable_data_pins_pullups();
}

/* Inline macro to read D2-D9 (PD2-PD7 and PB0-PB1). Keep the same
   name so existing call sites do not need to change. Wrap operands
   in parentheses to avoid surprises when expanded. */
#define read_data_pins() ((uint8_t)(((PIND) & 0b11111100) | ((PINB) & 0b00000011)))

// Send a 16-bit address via SPI to the shift registers
static void SPI_sendAddress(uint16_t address)
{
    SPI_send((address >> 8) & 0xFF); // Send high byte
    SPI_send(address & 0xFF);        // Send low byte
    PORTC |= _BV(RCLK_PIN);          // Set RCLK high
    PORTC &= ~_BV(RCLK_PIN);         // Set RCLK low
}

void writeCartByte(uint32_t address, uint8_t data)
{
    // Set data pins as output
    set_data_pins_output();

    // Send address
    SPI_sendAddress(address);

    // Write data to data pins
    PORTD = (PORTD & 0b00000011) | (data & 0b11111100); // D2-D7
    PORTB = (PORTB & 0b11111100) | (data & 0b00000011); // D8-D9

    // _CE low
    PORTC &= ~(_BV(_CE_PIN));

    // Pulse WR to write data, min delay is 40nS
    PORTC &= ~(_BV(_WR_PIN));
    PORTC |= _BV(_WR_PIN);

    // _CE high
    PORTC |= _BV(_CE_PIN);
}

uint8_t readRawCartByte(uint32_t address)
{
    uint8_t data;

    // Set data pins to input
    set_data_pins_input();

    // Send address
    SPI_sendAddress(address);
    // _CE low, _RD low
    PORTC &= ~(_BV(_CE_PIN) | _BV(_RD_PIN));

    // Read data from data pins
    // Add nop's to allow data to stabilize
    asm("nop\n"
        "nop\n");
    data = read_data_pins();

    // _CE high, _RD high
    PORTC |= _BV(_RD_PIN) | _BV(_CE_PIN);

    return data;
}

static uint8_t readCartByte(uint32_t address)
{
    uint8_t data;
    uint16_t offset;

    // Select bank/slot and compute offset
    translateAddress(address, &offset);

    // Set data pins to input
    set_data_pins_input();

    // Send address
    SPI_sendAddress(offset);
    // _CE low, _RD low
    PORTC &= ~(_BV(_CE_PIN) | _BV(_RD_PIN));

    // Read data from data pins
    // Add nop's to allow data to stabilize
    asm("nop\n"
        "nop\n");
    data = read_data_pins();

    // _CE high, _RD high
    PORTC |= _BV(_RD_PIN) | _BV(_CE_PIN);

    return data;
}

// Helper function to read a 16-bit pointer from header (file scope)
static uint16_t readHeaderPointer(uint16_t addr)
{
    return readCartByte(addr) | (readCartByte(addr + 1) << 8);
}

// Helper function to print a null-terminated string from ROM (file scope)
static void printROMString(uint16_t strPtr)
{
    if (strPtr == 0 || strPtr == 0xFFFF)
    {
        printf("(None)");
        return;
    }

    for (uint16_t i = strPtr;; i++)
    {
        uint8_t c = readCartByte(i);
        if (c == 0)
            break;
        if (c < 32)
            continue; // Skip control characters
        printf("%c", c);
    }
}

static uint16_t findSDSCHeader(void)
{
    // Search for SDSC signature in ROM
    // Common locations are 0x7FE0 and near the start of ROM
    const uint16_t searchLocations[] = {0x7FE0, 0x0000};
    const uint16_t searchRanges[] = {0x10, 0x100}; // How far to search from each location

    for (uint8_t loc = 0; loc < sizeof(searchLocations) / sizeof(searchLocations[0]); loc++)
    {
        uint16_t addr = searchLocations[loc];
        for (uint16_t i = 0; i < searchRanges[loc]; i++)
        {
            if (readCartByte(addr + i) == 'S' &&
                readCartByte(addr + i + 1) == 'D' &&
                readCartByte(addr + i + 2) == 'S' &&
                readCartByte(addr + i + 3) == 'C')
            {
                return addr + i;
            }
        }
    }
    return 0; // Return 0 if not found
}

static void displaySDSCHeader(void)
{
    uint16_t signature = findSDSCHeader();
    if (!signature)
    {
        printf(FG_RED "No SDSC ROM signature detected." COLOR_RESET "\n");
        return; // No SDSC header found
    }

    uint16_t headerAddr = signature + 4; // Skip "SDSC" signature
    printf("SDSC Header Information:\n");
    printf("Version: %d.%d\n", readCartByte(headerAddr + 0), readCartByte(headerAddr + 1));

    // Display release date from BCD format (DD MM YY YY)
    uint8_t day = readCartByte(headerAddr + 2);
    uint8_t month = readCartByte(headerAddr + 3);
    uint8_t yearLow = readCartByte(headerAddr + 4);
    uint8_t yearHigh = readCartByte(headerAddr + 5);

    // Convert from BCD
    day = ((day >> 4) & 0x0F) * 10 + (day & 0x0F);
    month = ((month >> 4) & 0x0F) * 10 + (month & 0x0F);
    uint16_t year = (((yearHigh >> 4) & 0x0F) * 1000) +
                    ((yearHigh & 0x0F) * 100) +
                    (((yearLow >> 4) & 0x0F) * 10) +
                    (yearLow & 0x0F);

    printf("Release Date: %04u.%02u.%02u\n", year, month, day);

    // Read all string pointers (they're stored sequentially)
    uint16_t authorPtr = readHeaderPointer(headerAddr + 6);
    uint16_t namePtr = readHeaderPointer(headerAddr + 8);
    uint16_t descPtr = readHeaderPointer(headerAddr + 10);

    // Display all strings using the same format
    printf("Author: ");
    printROMString(authorPtr);
    printf("\nName: ");
    printROMString(namePtr);
    printf("\nDescription: ");
    printROMString(descPtr);
    printf("\n");
}

static void displayROMHeader(void)
{
    // ROM Header starts at 0x7FF0
    uint16_t headerAddr = 0x7FF0;

    // Check "TMR SEGA" signature first
    uint8_t sig[8];
    for (int i = 0; i < 8; i++)
    {
        sig[i] = readCartByte(headerAddr + i);
    }

    move_to(yPos++, 40);

    // Verify we have the correct signature before displaying header
    if (memcmp(sig, "TMR SEGA", 8))
    {
        printf(FG_RED "No SEGA ROM signature detected." COLOR_RESET);
        return;
    }

    // Display the signature
    printf("ROM Signature: ");
    for (int i = 0; i < 8; i++)
    {
        printf("%c", sig[i]);
    }

    move_to(yPos++, 40);
    // Read product code and version
    printf("Product Code : %02X%02X",
           readCartByte(headerAddr + 0x0C),
           readCartByte(headerAddr + 0x0D));
    move_to(yPos++, 40);
    printf("Version      : %02X", readCartByte(headerAddr + 0x0E));

    // Read ROM size
    uint8_t romSizeCode = readCartByte(headerAddr + 0x0F) & 0x0F;
    move_to(yPos++, 40);
    printf("ROM Size     : ");
    switch (romSizeCode)
    {
    case 0xa:
        printf("8KB (Unused)");
        break;
    case 0xb:
        printf("16KB (Unused)");
        break;
    case 0xc:
        printf("32KB");
        break;
    case 0xd:
        printf("48KB (Unused, buggy)");
        break;
    case 0xe:
        printf("64KB (Rarely used)");
        break;
    case 0xf:
        printf("128KB");
        break;
    case 0x0:
        printf("256KB");
        break;
    case 0x1:
        printf("512KB (Rarely used)");
        break;
    case 0x2:
        printf("1MB (Unused, buggy)");
        break;
    default:
        printf("Unknown (0x%X)", romSizeCode);
        break;
    }

    // Read region code
    uint8_t region = readCartByte(headerAddr + 0x0F) >> 4;
    move_to(yPos++, 40);
    printf("Region       : ");
    switch (region)
    {
    case 0x3:
        printf("SMS Japan");
        break;
    case 0x4:
        printf("SMS Export");
        break;
    case 0x5:
        printf("Game Gear Japan");
        break;
    case 0x6:
        printf("Game Gear Export");
        break;
    case 0x7:
        printf("Game Gear International");
        break;
    default:
        printf("Unknown (0x%X)", region);
        break;
    }

    // Read checksum
    uint16_t checksum = (readCartByte(headerAddr + 0x0A) << 8) | readCartByte(headerAddr + 0x0B);
    move_to(yPos++, 40);
}

// Program a byte to the flash at the specified address
//
// Programming is always performed by selecting the appropriate bank and slot
// presented at offset 0x8000.
static void progCartByte(uint32_t address, uint8_t data)
{

    uint16_t offset;

    // Select bank/slot and compute offset
    translateAddress(address, &offset);

    writeCartByte(0x5555, 0xaa); // Unlock command
    writeCartByte(0x2aaa, 0x55); // Unlock command
    writeCartByte(0x5555, 0xa0); // Write command
    writeCartByte(offset, data); // Write data byte
    _delay_us(10);
}

static void processBlock(uint8_t *block, uint16_t length)
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
        move_to(yPos++, 3);
        printf("Manufacturer: SST (MCHP)");
        switch (deviceID)
        {
        case 0xB5:
            move_to(yPos++, 3);
            printf(" (SST39SF010)");
            flashSize = ((uint32_t)128 * (uint32_t)1024); // 128KB
            break;
        case 0xB6:
            move_to(yPos++, 3);
            printf(" (SST39SF020)");
            flashSize = ((uint32_t)256 * (uint32_t)1024); // 256KB
            break;
        case 0xB7:
            move_to(yPos++, 3);
            printf(" (SST39SF040)");
            flashSize = ((uint32_t)512 * (uint32_t)1024); // 512KB
            break;
        default:
            move_to(yPos++, 3);
            printf("Device      : Unknown (0x%02X)", deviceID);
            break;
        }
        break;
    default:
        move_to(yPos++, 3);
        printf(FG_RED "No flash device detected." COLOR_RESET);
        return;
        break;
    }

    move_to(yPos++, 3);
    printf("Flash Size  : %luKB", flashSize / 1024);
    move_to(yPos++, 3);
    printf("Flash CRC32 : 0x%08lX", flashCRC32);
    move_to(yPos++, 3);
    printf("Prog. CRC32 : 0x%08lX", progCRC32);
    move_to(yPos++, 3);
    printf("Mapper      : %s", getCurrentMapperName());
}

static void eraseFlash(void)
{
    printf("Erasing flash...\n");
    writeCartByte(0x5555, 0xaa); // Unlock command
    writeCartByte(0x2aaa, 0x55); // Unlock command
    writeCartByte(0x5555, 0x80); // Erase command
    writeCartByte(0x5555, 0xaa); // Unlock command
    writeCartByte(0x2aaa, 0x55); // Unlock command
    writeCartByte(0x5555, 0x10); // Chip erase command
    _delay_ms(100);              // Wait for erase to complete
    printf("Erase complete.\n");
}

static void xmodemProgramFlash(void)
{
    uint32_t downloadedSize;

    printf("Starting XMODEM file receive for programming...\n");
    // Program Flash
    flashAddress = 0;
    progCRC32 = 0xFFFFFFFF;
    downloadedSize = XMODEM_ReceiveFile(buffer, processBlock);
    // Calculate checksum over remaining flash size as downloaded
    // file may be smaller than flash size
    for (uint32_t addr = downloadedSize; addr < flashSize; addr++)
    {
        uint8_t data = readCartByte(addr);
        updateCRC32(&progCRC32, data);
    }
    printf("Programming complete. Programmed CRC32: 0x%08lX\n", progCRC32);
}

static void processSendBlock(uint8_t *block, uint32_t start, uint16_t length)
{
    // process the received block (e.g., write to flash)
    while (length--)
    {
        *block = readCartByte(start++);
        updateCRC32(&flashCRC32, *block);
        block++;
    }
}

static void xmodemReadFlash(void)
{
    uint32_t bytesSent;

    flashCRC32 = 0xFFFFFFFF;
    printf("Starting XMODEM file send for flash read...\n");
    bytesSent = XMODEM_SendFile(buffer, flashSize, processSendBlock);
    printf("Read complete (%lu bytes sent). CRC32: 0x%08lX\n", bytesSent, flashCRC32);
}

static void checksumFlash(void)
{
    printf("Checksuming flash...\n");
    flashCRC32 = 0xFFFFFFFF;
    for (uint32_t addr = 0; addr < flashSize; addr++)
    {
        uint8_t data = readCartByte(addr);
        updateCRC32(&flashCRC32, data);
    }
    if (flashCRC32 == progCRC32)
    {
        printf(FG_GREEN "Flash verification successful. CRC32 matches: 0x%08lX" COLOR_RESET "\n", flashCRC32);
    }
    else
    {
        printf(FG_RED "Flash verification failed. Expected CRC32: 0x%08lX, Read CRC32: 0x%08lX" COLOR_RESET "\n", progCRC32, flashCRC32);
    }
}

void draw_box_frame(int start_row, int start_col, int width, int height)
{
    // top border
    move_to(start_row, start_col);
    printf("%s", TL);
    for (int i = 0; i < width - 2; ++i)
        printf("%s", H);
    printf("%s", TR);

    // inner rows (no highlights)
    for (int r = 0; r < height - 2; ++r)
    {
        move_to(start_row + 1 + r, start_col);
        printf("%s", V);
        move_to(start_row + 1 + r, start_col + width - 1);
        printf("%s", V);
    }

    // bottom border
    move_to(start_row + height - 1, start_col);
    printf("%s", BL);
    for (int i = 0; i < width - 2; ++i)
        printf("%s", H);
    printf("%s", BR);
}

/* Draw banner title */
void draw_banner(uint8_t *start_row, uint8_t start_col)
{
    move_to((*start_row)++, start_col);
    printf(FG_CYAN "  SMS FLASH PROGRAMMER (C)2025, IrataHack. All Rights Reserved." COLOR_RESET);
}

/* Helper to repaint the UI after selection or at startup */
void repaint_ui(uint8_t start_row, uint8_t start_col, uint8_t width, uint8_t height)
{
    clear_screen();
    draw_banner(&start_row, start_col);
    draw_box_frame(start_row, start_col, width, height);
    fflush(stdout);
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

    mapperIndex = detectMapper();
    setMapper(&mappers[mapperIndex]);

    for (;;)
    {
        hide_cursor();
        repaint_ui(1, 1, 80, 19);
        yPos = 3;
        getFlashID();

        yPos = 3;
        displayROMHeader();

        yPos = 9;
        move_to(yPos++, 1);
        printf("%s", LH);
        for (int i = 0; i < 78; i++)
            printf("%s", H);
        printf("%s", RH);

        move_to(yPos++, 3);
        printf("1 ........ Erase");
        move_to(yPos++, 3);
        printf("2 ........ Blank Check");
        move_to(yPos++, 3);
        printf("3 ........ Program (XMODEM-1K download)");
        move_to(yPos++, 3);
        printf("4 ........ Checksum");
        move_to(yPos++, 3);
        printf("5 ........ Read Byte");
        move_to(yPos++, 3);
        printf("6 ........ Program Byte");
        move_to(yPos++, 3);
        printf("7 ........ Read ROM (XMODEM-1K upload)");
        move_to(yPos++, 3);
        printf("8 ........ Display SDSC ROM Header");
        move_to(yPos++, 3);
        printf("9 ........ Erase, Program (XMODEM-1K download), and Verify");
        move_to(yPos++, 3);
        printf("0 ........ Change Mapper (Current: %s)", getCurrentMapperName());
        move_to(yPos++, 3);
        printf("Select an option: ");

        input = getchar();

        clear_screen();
        show_cursor();

        switch (input)
        {
        case '1':
            eraseFlash();
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '2':
        {
            uint32_t addr;
            // Blank Check Flash
            printf("Performing blank check...\n");
            for (addr = 0; addr < flashSize; addr++)
            {
                if (readCartByte(addr) != 0xFF)
                {
                    printf(FG_RED "Flash is NOT blank. First non-blank byte at address 0x%06lX: 0x%02X" COLOR_RESET "\n", addr, readCartByte(addr));
                    break;
                }
            }
            if (addr == flashSize)
            {
                printf(FG_GREEN "Blank check successful." COLOR_RESET "\n");
            }
            printf("Press any key to continue...\n");
            getchar();
        }
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
            printf("Enter address to read (hex): 0x");
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
            printf("Enter address to write (hex): 0x");
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
        case '8':
            displaySDSCHeader();
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '9':
            eraseFlash();
            xmodemProgramFlash();
            checksumFlash();
            printf("Press any key to continue...\n");
            getchar();
            break;
        case '0':
            mapperIndex = (mapperIndex + 1) % (sizeof(mappers) / sizeof(mappers[0]));
            setMapper(&mappers[mapperIndex]);
            break;
        default:
            break;
        }
    }
}
