# Project Guidelines

## Overview

AVR C firmware for a Sega Master System flash cartridge programmer. The device connects via USB/serial and provides a menu-driven interface for reading, erasing, programming, and verifying NOR flash chips on SMS cartridges. Uses Arduino (ATmega328p) hardware with XMODEM protocol for ROM transfers.

## Code Style

- **Language**: Embedded C for AVR (ATmega328p @ 16MHz)
- **Naming**: snake_case for functions, UPPER_CASE for macros, camelCase for variables
- **Module prefixes**: Functions use module prefixes (`SPI_`, `XMODEM_`, mapper-specific)
- **String storage**: Use `printf()` macro from [src/main.c](src/main.c#L10) that wraps `PSTR()` to store strings in program memory (flash) instead of RAM
- **Hardware access**: Direct register manipulation (e.g., `PORTC |= _BV(pin)`, `DDRD &= ~(mask)`)
- **File scope**: Use `static` for file-local functions and variables; use `extern` declarations for cross-module functions
- **Comments**: Inline for bit manipulation; multi-line for complex algorithms

## Architecture

### Mapper System
- **Core abstraction**: [src/mappers.h](src/mappers.h) defines `Mapper_t` struct with function pointers
- **Implementations**: SEGA standard mapper and Iratahack mapper in [src/mappers.c](src/mappers.c)
- **Address translation**: Bank switching logic handled by `translateAddress()` to map 32-bit flash addresses to 16-bit cart bus addresses
- **Detection**: Auto-detect mapper type at boot via `detectMapper()`

### Hardware Interface
- **Address output**: SPI (via 74HC595 shift registers) drives A0-A15 bus lines
- **Data bus**: Direct GPIO on D2-D9 (PD2-PD7, PB0-PB1) - bidirectional 8-bit
- **Control signals**: _CE, _RD, _WR on PC1-PC3; RCLK on PC0
- **Read/write primitives**: `readCartByte()`, `writeCartByte()` in [src/main.c](src/main.c)

### Module Structure
- [src/main.c](src/main.c) — Hardware init, menu loop, flash operations, UI rendering
- [src/mappers.c](src/mappers.c) — Cartridge mapper implementations
- [src/xmodem.c](src/xmodem.c) — XMODEM protocol (Atmel-derived code)
- [src/uart.c](src/uart.c), [src/timer.c](src/timer.c) — Serial and timing peripherals
- [src/CRC32.c](src/CRC32.c) — Checksum verification

## Build and Test

### Prerequisites
```bash
# Install AVR toolchain
sudo apt-get install gcc-avr avr-libc avrdude
```

### Build Commands
```bash
cd src
make                     # Compile firmware -> SMSFlasher.bin
make SMSFlasher.hex      # Generate Intel HEX format
make download            # Flash to device via /dev/ttyUSB0
make clean               # Remove build artifacts
```

### WSL Setup (Windows Users)
```powershell
# In admin PowerShell on Windows
usbipd list
usbipd attach --wsl --busid <BUSID>
```
If USBIP error occurs, run in WSL: `sudo modprobe vhci_hcd`

### Testing
- Connect via serial terminal (115200 baud): `screen /dev/ttyUSB0 115200`
- Use menu options to test hardware (option 5: Read Byte is safe for testing)
- For XMODEM transfers, use `lrzsz` tools (`sx` to send, `rx` to receive)

## Project Conventions

### Memory Constraints
- ATmega328p has only 2KB RAM — minimize string literals in RAM
- Store all constant strings in program memory using `PSTR()` or the custom `printf()` macro
- Reuse buffers (e.g., `buffer[1024]` for XMODEM)

### Hardware Timing
- Critical paths use inline assembly `nop` for bus timing (see `readRawCartByte()`)
- SPI clock is F_CPU/2 (8MHz at 16MHz system clock)
- Flash programming uses standard AMD/SST command sequences (unlock, write, verify)

### ANSI Terminal UI
- Uses ANSI escape codes for colors, cursor positioning, and box drawing
- See color defines (e.g., `FG_RED`, `BG_BLUE`) and Unicode box chars in [src/main.c](src/main.c#L36-L65)
- Use `printf()` + `fflush(stdout)` after cursor movement

### Callback Pattern
- XMODEM functions take `void (*processBlock)(...)` callbacks for per-block processing
- Used to write/read data incrementally without full-buffer allocation

## Integration Points

### Serial Protocol
- **Baud rate**: 115200 (configured in `initUART()`)
- **Flow control**: None
- **XMODEM**: 128-byte packets with CRC16 checksums

### Flash Chip Support
- Auto-detect via CFI/JEDEC ID (`getFlashID()` in [src/main.c](src/main.c))
- Tested with SST39SF/AM29F series NOR flash
- Erase: Full chip erase command (0x80, 0xAA, 0x55, 0x10 sequence)
- Program: Byte-level programming with unlock sequences

## Security

- **No authentication**: Serial menu is fully open
- **Hardware access**: Tool has direct flash write/erase capabilities
- **Flash corruption risk**: Always blank-check before programming to avoid partial writes
- **CRC verification**: Use menu option 4 after programming to verify integrity
