#include <stddef.h>
#include <stdint.h>
#include "mappers.h"

extern void writeCartByte(uint32_t address, uint8_t data);

static Mapper_t *currentMapper = NULL;
static uint8_t currentBank;
static uint8_t currentSlot;

static void SEGA_mapperInit(void);
static void SEGA_translateAddress(uint32_t address, uint16_t *offset);
static void MultiGame_mapperInit(void);
static void MultiGame_translateAddress(uint32_t address, uint16_t *offset);

Mapper_t mappers[] = {
    {"SEGA", SEGA_mapperInit, SEGA_translateAddress},
    {"Multi-game", MultiGame_mapperInit, MultiGame_translateAddress}
};

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
