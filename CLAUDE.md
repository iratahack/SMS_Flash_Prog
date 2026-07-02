# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

AVR C firmware for a Sega Master System flash cartridge programmer built on an Arduino (ATmega328p @ 16 MHz). The device exposes a serial menu for identifying, erasing, programming, and verifying NOR flash on SMS cartridges, and transfers ROM images over XMODEM.

## Build

Requires the AVR toolchain: `sudo apt-get install gcc-avr avr-libc avrdude`.

```bash
cd src
make                        # -> SMSFlasher.bin
make SMSFlasher.bin.hex     # Intel HEX (what CI releases)
make download               # avrdude -> /dev/ttyUSB0 @ 115200
make clean
make dis                    # disassembly via avr-objdump
```

`74hc595/` is a separate standalone test firmware for the address-bus shift registers; it has its own `Makefile` that builds `SPIShifter.bin` the same way.

There is no test suite. Verification is done on real hardware: connect a serial terminal to `/dev/ttyUSB0` at **1,000,000 baud** (see `BAUD` in `src/uart.c`), then exercise menu options (option 5 "Read Byte" is non-destructive). XMODEM transfers use `sx rom.bin` to send, `rx dump.bin` to receive.

## CI / releases

`.github/workflows/build.yml` builds `SMSFlasher.bin.hex` on push to `main`/`develop`. A GitHub release is only created when a `release/vX.Y.Z` tag is pushed (or the workflow is manually dispatched with `create_release=true`), publishing `SMSFlasher.bin` and `SMSFlasher.bin.hex`.

## Architecture

### Hardware bus

The cartridge's 16-bit address bus is driven by two cascaded 74HC595 shift registers over the ATmega's SPI (MOSI/SCK/SS on PB3/PB5/PB2). A high pulse on `RCLK` (PC0) latches the shifted address. Data bus D0–D7 is bidirectional GPIO on PD2–PD7 + PB0–PB1 (toggled input/output per read/write). Control lines `_CE`/`_RD`/`_WR` are on PC1/PC2/PC3. This means any bus access is: SPI-shift address → pulse RCLK → set data direction → toggle control strobes → read/write port. Bus timing inside `readRawCartByte`/`writeCartByte` relies on inline `nop`s — don't restructure those without measuring.

### Mapper abstraction (`src/mappers.{h,c}`)

`Mapper_t` is a vtable of `{name, init, translateAddress, detect}`. Two implementations: **SEGA** (standard mapper, 5-bit bank in slot 2 via `0xFFFF`) and **Iratahack** (3-bit bank + 2-bit game slot via `0xFFFE`/`0xFFFF`). `translateAddress()` converts a flat 32-bit flash address into a 14-bit offset within slot 2 (`0x8000` base) plus any bank/slot register writes — but only writes the mapper register when the bank/slot actually changed, since every write costs a full bus cycle. Auto-detection runs at boot (`detectMapper()` probes by writing 0s to all slot registers and comparing slot contents); the menu lets the user override.

### `src/main.c`

Contains all hardware init, the menu loop, flash command sequences (AMD/SST-style unlock: `0xAAAA=0xAA`, `0x5555=0x55`, …), the ANSI/Unicode-box UI renderer, and the per-block XMODEM callbacks. The first 70 lines define the `printf`→`printf_P(PSTR(...))` wrapper and ANSI/box-drawing macros — stringification of every format string into flash is critical on a 2 KB-RAM part, so **all format strings must go through the `printf` macro** (or `PSTR` directly). A single 1 KB `buffer[]` is reused for XMODEM packets; do not add parallel buffers without checking the map file.

### `src/xmodem.c`

Derived from Atmel sample code. `XMODEM_ReceiveFile` / `XMODEM_SendFile` both take a `processBlock` callback invoked per 128-byte packet, so main.c streams flash writes/reads without buffering the full ROM.

## Conventions

- Naming: `snake_case` functions, `UPPER_CASE` macros, `camelCase` variables. Module prefixes (`SPI_`, `XMODEM_`, `SEGA_`, `MultiGame_`) are used for namespacing — keep new code consistent.
- `static` for everything file-local; cross-module functions are declared `extern` at the top of the consuming `.c` file rather than via shared headers (except `mappers.h`).
- Direct register manipulation (`PORTC |= _BV(pin)`, `DDRD &= ~mask`) — no HAL.
- Build flags are `-O3`; code assumes the optimizer inlines small helpers. Don't add `volatile` without a reason.

## WSL serial setup

When running under WSL on Windows, attach the Arduino's COM port from an admin PowerShell before building/flashing:

```powershell
usbipd list
usbipd attach --wsl --busid <BUSID>
```

If `usbipd` reports "WSL kernel is not USBIP capable" after `wsl --update`, run `sudo modprobe vhci_hcd` inside WSL.
