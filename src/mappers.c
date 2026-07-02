#include <stddef.h>
#include "common.h"
#include "mappers.h"

extern void writeCartByte(uint32_t address, uint8_t data);
extern uint8_t readRawCartByte(uint32_t address);

static Mapper_t *currentMapper = NULL;
static uint8_t currentBank;
static uint8_t currentSlot;

static void SEGA_mapperInit(void);
static void SEGA_translateAddress(uint32_t address, uint16_t *offset);
static void MultiGame_mapperInit(void);
static void MultiGame_translateAddress(uint32_t address, uint16_t *offset);
static bool SEGA_detect(void);
static bool MultiGame_detect(void);

Mapper_t mappers[] = {
    {"SEGA", SEGA_mapperInit, SEGA_translateAddress, SEGA_detect},
    {"Iratahack", MultiGame_mapperInit, MultiGame_translateAddress, MultiGame_detect}};

void mapperInit(void)
{
    if (currentMapper && currentMapper->init)
    {
        currentMapper->init();
    }
    currentBank = 0xff;
    currentSlot = 0xff;
}

// Set the current mapper
void setMapper(Mapper_t *mapper)
{
    currentMapper = mapper;
    mapperInit();
}

// Get the current mapper
Mapper_t *getCurrentMapper(void)
{
    return currentMapper;
}

// Get the name of the current mapper
char *getCurrentMapperName(void)
{
    if (currentMapper)
    {
        return (char *)currentMapper->name;
    }
    else
    {
        return "None";
    }
}

void translateAddress(uint32_t address, uint16_t *offset)
{
    if (currentMapper && currentMapper->translateAddress)
    {
        currentMapper->translateAddress(address, offset);
    }
    else
    {
        *offset = (uint16_t)(address & 0xFFFF);
    }
}

uint8_t detectMapper(void)
{
    for (uint8_t i = 0; i < sizeof(mappers) / sizeof(Mapper_t); i++)
    {
        if (mappers[i].detect())
        {
            return i;
        }
    }
    return 0; // Default to SEGA mapper if none detected
}

static void SEGA_mapperInit(void)
{
    writeCartByte(0xfffe, 0x01); // Slot 1
    writeCartByte(0xffff, 0x02); // Slot 2
}

static void MultiGame_mapperInit(void)
{
    writeCartByte(0xfffe, 0x00); // Game
    writeCartByte(0xffff, 0x02); // Slot 2
}

static void SEGA_translateAddress(uint32_t address, uint16_t *offset)
{
    uint8_t newBank = (address >> 14) & 0x1f;
    *offset = (address & 0x3FFF) | 0x8000;

    /* Only update bank/slot if they changed since last selection. */
    if (newBank != currentBank)
    {
        writeCartByte(0xffff, newBank);
        currentBank = newBank;
    }
}

static void MultiGame_translateAddress(uint32_t address, uint16_t *offset)
{
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

static bool SEGA_detect(void)
{
    uint8_t data[3];

    // Set all slots to 0
    writeCartByte(0xfffd, 0x00); // Slot 0
    writeCartByte(0xfffe, 0x00); // Slot 1
    writeCartByte(0xffff, 0x00); // Slot 2

    // All 16KB in all 3 slots should be identical for SEGA mapper
    for (uint32_t addr = 0x0000; addr < 0x4000; addr++)
    {
        data[0] = readRawCartByte(addr | 0x0000); // Slot 0
        data[1] = readRawCartByte(addr | 0x4000); // Slot 1
        data[2] = readRawCartByte(addr | 0x8000); // Slot 2
        if (data[0] != data[1] || data[0] != data[2])
        {
            return false;
        }
    }
    return true;
}

static bool MultiGame_detect(void)
{
    uint8_t data[3];

    writeCartByte(0xfffe, 0x00); // Slot 1
    writeCartByte(0xffff, 0x00); // Slot 2

    // All 16KB in all 3 slots should be identical for SEGA mapper
    for (uint32_t addr = 0x0000; addr < 0x4000; addr++)
    {
        data[1] = readRawCartByte(addr | 0x4000); // Slot 1
        data[2] = readRawCartByte(addr | 0x8000); // Slot 2
        if (data[1] != data[2])
        {
            return true;
        }
    }
    return false;
}
