//
// Copyright (c) 2013 Christopher Baker <https://christopherbaker.net>
//
// SPDX-License-Identifier:	MIT
//

#include <stdio.h>
#include <avr/pgmspace.h>

// Conditionally use pgm memory if it is available.

#if defined(PROGMEM)
#define FLASH_PROGMEM PROGMEM
#define FLASH_READ_DWORD(x) (pgm_read_dword_near(x))
#else
#define FLASH_PROGMEM
#define FLASH_READ_DWORD(x) (*(uint32_t *)(x))
#endif

static const uint32_t crc32_table[] FLASH_PROGMEM = {
    0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac,
    0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
    0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
    0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};

void updateCRC32(uint32_t *crc, const uint8_t data)
{
    // via http://forum.arduino.cc/index.php?topic=91179.0
    uint8_t tbl_idx = 0;

    tbl_idx = *crc ^ (data >> (0 * 4));
    *crc = FLASH_READ_DWORD(crc32_table + (tbl_idx & 0x0f)) ^ (*crc >> 4);
    tbl_idx = *crc ^ (data >> (1 * 4));
    *crc = FLASH_READ_DWORD(crc32_table + (tbl_idx & 0x0f)) ^ (*crc >> 4);
}
