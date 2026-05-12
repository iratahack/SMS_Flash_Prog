# SEGA Master System Flash Cartridge Programmer

A small USB/Arduino-based programmer and diagnostic tool for Sega Master System ROM cartridges.
It provides a serial-menu interface to identify the cartridge flash, erase and program NOR flash chips,
read or write individual bytes, perform blank checks, and transfer full ROM images using XMODEM-1K.
This repository contains the firmware source (AVR C) used on the programmer hardware and example
host-side workflows for sending/receiving ROM images.

## Hardware

The programmer is an ATmega328p (Arduino Uno / Nano) running at 16 MHz. It accesses the SMS
cartridge slot through the following bus signals:

- **Address bus (A0–A15)**: driven by two cascaded 74HC595 shift registers clocked over the ATmega's SPI peripheral (MOSI=PB3, SCK=PB5, SS=PB2). A high pulse on **RCLK** (PC0) latches the shifted address onto the cartridge bus.
- **Data bus (D0–D7)**: bidirectional GPIO on Arduino pins D2–D9 (PD2–PD7 and PB0–PB1). Direction is toggled per read/write.
- **Control signals**: `_CE` on PC1, `_RD` on PC2, `_WR` on PC3 (all active low).

KiCad design files for the programmer shield — including schematic, PCB, and Gerbers — live in
[`Arduino_SMS_Cart_Programmer/`](Arduino_SMS_Cart_Programmer/).

### Supported flash chips

The firmware auto-detects the flash device using the AMD/SST JEDEC ID command sequence and
currently recognises the SST39SF family:

| Manufacturer ID | Device ID | Part       | Size  |
| --------------- | --------- | ---------- | ----- |
| `0xBF` (SST)    | `0xB5`    | SST39SF010 | 128 KB |
| `0xBF` (SST)    | `0xB6`    | SST39SF020 | 256 KB |
| `0xBF` (SST)    | `0xB7`    | SST39SF040 | 512 KB |

### Supported mappers

The firmware auto-detects the cartridge mapper at boot and can be switched manually from the menu:

- **SEGA** — standard 16 KB-banked SEGA mapper (bank register at `0xFFFF`).
- **Iratahack** — 3-bit bank + 2-bit game slot (`0xFFFE` / `0xFFFF`) for multi-game carts.

## Building the firmware

Install the AVR toolchain:

```sh
sudo apt-get install gcc-avr avr-libc avrdude
```

Build from the `src/` directory:

```sh
cd src
make                    # compile -> SMSFlasher.bin
make SMSFlasher.bin.hex # Intel HEX output (flashable with avrdude)
make clean              # remove build artifacts
make dis                # disassembly via avr-objdump | less
```

A separate diagnostic firmware that exercises just the 74HC595 address-bus shift registers
lives in [`74hc595/`](74hc595/) and builds with its own `Makefile` in the same way.

Pre-built releases are published on the GitHub Releases page when a `release/vX.Y.Z` tag
is pushed; each release includes `SMSFlasher.bin` and `SMSFlasher.bin.hex`.

## Flashing the firmware to the Arduino

With the Arduino connected as `/dev/ttyUSB0`:

```sh
cd src
make download
```

This runs `avrdude -c arduino -p atmega328p -P /dev/ttyUSB0 -b 115200` against
`SMSFlasher.bin.hex`. Override the port/programmer by editing the `download` target
if your setup differs.

### Arduino COM port setup for WSL

After connecting the programmer via USB to a Windows machine, enter the command below in an
administrator PowerShell to attach the Arduino COM port from Windows to WSL:

```sh
usbipd list # look for USB-SERIAL CH340 (COM4) or similar
usbipd bind --busid <BUSIS> # as administrator
usbipd attach --wsl --busid <BUSID>
```

If the error below is displayed and you already have the latest install of WSL try
`sudo modprobe vhci_hcd` from WSL:

```
usbipd: info: Using WSL distribution 'Ubuntu-22.04' to attach; the device will be available in all WSL 2 distributions.
usbipd: error: WSL kernel is not USBIP capable; update with 'wsl --update'.
```

## Using the programmer

Once the firmware is running, connect a serial terminal to the device at **2,000,000 baud**,
8-N-1, no flow control. The UI uses ANSI colour and Unicode box-drawing characters, so
use a terminal that supports both (e.g. `picocom`, `minicom`, or the serial client in
Windows Terminal):

```sh
picocom -b 1000000 /dev/ttyUSB0
```

The firmware redraws a full-screen menu on each iteration:

| Key | Action |
| --- | ------ |
| `1` | Erase flash (full-chip erase) |
| `2` | Blank check — scan entire flash for `0xFF` |
| `3` | Program flash (receive ROM image over XMODEM-1K) |
| `4` | Checksum — compute CRC32 of flash and compare against last programmed CRC32 |
| `5` | Read a single byte from a hex address |
| `6` | Program a single byte at a hex address |
| `7` | Read ROM — send full flash contents to host over XMODEM-1K |
| `8` | Display SDSC ROM header (if present) |
| `9` | Erase, program (XMODEM-1K), and verify in one step |
| `0` | Toggle between detected mappers (SEGA / Iratahack) |

The flash size used by operations `2`, `4`, `7`, and `9` is determined by the JEDEC ID
read at each menu redraw, so the programmer adapts automatically to whichever SST39SF
part is installed.

### Transferring ROM images over XMODEM

The firmware uses **XMODEM-1K** (1024-byte packets with CRC-16). The receive path also
accepts classic 128-byte XMODEM packets, but the send path always transmits 1K blocks.
Use a host XMODEM implementation such as `lrzsz` (`sx` / `rx`) or the built-in XMODEM
support in `picocom`/`minicom`.

Program a ROM image (select menu option `3` on the device first, then from the host):

```sh
sx rom.bin < /dev/ttyUSB0 > /dev/ttyUSB0
```

Read a full dump from the device (select menu option `7` first):

```sh
rx cart_dump.bin < /dev/ttyUSB0 > /dev/ttyUSB0
```

If you use `picocom`, you can bind send/receive to a keystroke via `--send-cmd "sx -vv"`
and `--receive-cmd "rx -vv"`, which avoids having to detach from the serial session.

## Repository layout

- [`src/`](src/) — programmer firmware (main target).
- [`74hc595/`](74hc595/) — standalone test firmware that cycles the address-bus shift registers.
- [`Arduino_SMS_Cart_Programmer/`](Arduino_SMS_Cart_Programmer/) — KiCad hardware design files.
- [`.github/workflows/build.yml`](.github/workflows/build.yml) — CI build and release workflow.
