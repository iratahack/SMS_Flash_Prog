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

static void writeCartByte(uint32_t address, uint8_t data)
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

// Helper: select bank/slot derived from a full flash address and return offset
static void selectBankSlot(uint32_t address, uint16_t *offset)
{
    /* Keep track of the currently-selected bank/slot so we avoid
       writing the same values repeatedly (saves SPI / write cycles).
       Initialize to 0xFF so the first call always programs them. */
    static uint8_t currentBank = 0xFF;
    static uint8_t currentSlot = 0xFF;
    uint8_t newBank = (address >> 14) & 0x07;
    uint8_t newSlot = (address >> 17) & 0x03;
    *offset = (address & 0x3FFF) | 0x8000;

    /* Only update bank/slot if they changed since last selection. */
    if (newBank != currentBank)
    {
        writeCartByte(0xffff, newBank);
        currentBank = newBank;
    }
    if (newSlot != currentSlot)
    {
        writeCartByte(0xfffe, newSlot);
        currentSlot = newSlot;
    }
}

static uint8_t readCartByte(uint32_t address)
{
    uint8_t data;
    uint16_t offset;

    // Select bank/slot and compute offset
    selectBankSlot(address, &offset);

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
        return; // No SDSC header found
    }

    uint16_t headerAddr = signature + 4; // Skip "SDSC" signature
    printf("\n\nSDSC Header Information:\n");
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
    // Check for and display SDSC header if present
    displaySDSCHeader();

    // ROM Header starts at 0x7FF0
    uint16_t headerAddr = 0x7FF0;

    // Check "TMR SEGA" signature first
    uint8_t sig[8];
    for (int i = 0; i < 8; i++)
    {
        sig[i] = readCartByte(headerAddr + i);
    }

    // Verify we have the correct signature before displaying header
    if (sig[0] != 'T' || sig[1] != 'M' || sig[2] != 'R' || sig[3] != ' ' ||
        sig[4] != 'S' || sig[5] != 'E' || sig[6] != 'G' || sig[7] != 'A')
    {
        printf("\nNo SEGA ROM signature detected.\n");
        return;
    }

    printf("\nReading SEGA ROM Header...\n");

    // Display the signature
    printf("Signature: ");
    for (int i = 0; i < 8; i++)
    {
        printf("%c", sig[i]);
    }
    printf("\n");

    // Read product code and version
    printf("Product Code: %02X%02X\n",
           readCartByte(headerAddr + 0x0C),
           readCartByte(headerAddr + 0x0D));
    printf("Version: %02X\n", readCartByte(headerAddr + 0x0E));

    // Read ROM size
    uint8_t romSizeCode = readCartByte(headerAddr + 0x0F) & 0x0F;
    printf("ROM Size: ");
    switch (romSizeCode)
    {
    case 0xa:
        printf("8KB (Unused)\n");
        break;
    case 0xb:
        printf("16KB (Unused)\n");
        break;
    case 0xc:
        printf("32KB\n");
        break;
    case 0xd:
        printf("48KB (Unused, buggy)\n");
        break;
    case 0xe:
        printf("64KB (Rarely used)\n");
        break;
    case 0xf:
        printf("128KB\n");
        break;
    case 0x0:
        printf("256KB\n");
        break;
    case 0x1:
        printf("512KB (Rarely used)\n");
        break;
    case 0x2:
        printf("1MB (Unused, buggy)\n");
        break;
    default:
        printf("Unknown (0x%X)\n", romSizeCode);
        break;
    }

    // Read region code
    uint8_t region = readCartByte(headerAddr + 0x0F) >> 4;
    printf("Region: ");
    switch (region)
    {
    case 0x3:
        printf("SMS Japan\n");
        break;
    case 0x4:
        printf("SMS Export\n");
        break;
    case 0x5:
        printf("Game Gear Japan\n");
        break;
    case 0x6:
        printf("Game Gear Export\n");
        break;
    case 0x7:
        printf("Game Gear International\n");
        break;
    default:
        printf("Unknown (0x%X)\n", region);
        break;
    }

    // Read checksum
    uint16_t checksum = (readCartByte(headerAddr + 0x0A) << 8) | readCartByte(headerAddr + 0x0B);
    printf("Checksum: 0x%04X\n", checksum);
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
    printf("\nStarting XMODEM file send for flash read...\n");
    bytesSent = XMODEM_SendFile(buffer, flashSize, processSendBlock);
    printf("Read complete (%lu bytes sent). CRC32: 0x%08lX\n", bytesSent, flashCRC32);
}

static void checksumFlash(void)
{
    printf("\nChecksuming flash...\n");
    flashCRC32 = 0xFFFFFFFF;
    for (uint32_t addr = 0; addr < flashSize; addr++)
    {
        uint8_t data = readCartByte(addr);
        updateCRC32(&flashCRC32, data);
    }
    if (flashCRC32 == progCRC32)
    {
        printf("\n\033[32mFlash verification successful. CRC32 matches: 0x%08lX\033[0m\n", flashCRC32);
    }
    else
    {
        printf("\n\033[31mFlash verification failed. Expected CRC32: 0x%08lX, Read CRC32: 0x%08lX\033[0m\n", progCRC32, flashCRC32);
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
        printf("8 ........ Display ROM Header\n");
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
        {
            uint32_t addr;
            // Blank Check Flash
            printf("\nPerforming blank check...\n");
            for (addr = 0; addr < flashSize; addr++)
            {
                if (readCartByte(addr) != 0xFF)
                {
                    printf("\nFlash is NOT blank. First non-blank byte at address 0x%06lX: 0x%02X\n", addr, readCartByte(addr));
                    break;
                }
            }
            if (addr == flashSize)
            {
                printf("\nBlank check successful.\n");
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
        case '8':
            displayROMHeader();
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
