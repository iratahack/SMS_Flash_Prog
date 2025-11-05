# SEGA Master System Flash Cartridge Programmer

A small USB/Arduino-based programmer and diagnostic tool for Sega Master System ROM cartridges.
It provides a serial-menu interface to identify the cartridge flash, erase and program NOR flash chips,
read or write individual bytes, perform blank checks, and transfer full ROM images using XMODEM.
This repository contains the firmware source (AVR C) used on the programmer hardware and example
host-side workflows for sending/receiving ROM images.

## Arduino COM Port Setup for WSL

After connecting the programmer via USB to a Windows machine, enter the command below in an administrator PowerShell to attach the Arduino COM port from Windoes to WLS.

```sh
usbipd list # look for USB-SERIAL CH340 (COM4) or simalar
usbipd attach --wsl --busid <BUSID>
```

If the error below is displayed and you already have the latest install of WSL try `sudo modprobe vhci_hcd` from WSL.

```sh
usbipd: info: Using WSL distribution 'Ubuntu-22.04' to attach; the device will be available in all WSL 2 distributions.
usbipd: error: WSL kernel is not USBIP capable; update with 'wsl --update'.
```

## Menu Options

The firmware presents a simple serial menu. Below are the options shown in `main()` and what each one does.

- 1 — Erase Flash
  - Performs a full chip erase sequence (unlock + chip-erase). This sets all bytes to 0xFF and is typically done before programming a new ROM image.

- 2 — Blank Check Flash
  - Scans the entire detected flash size and verifies every byte equals 0xFF. If a non-blank byte is found the menu reports the first non-blank address and byte value.

- 3 — Program Flash (XMODEM download)
  - The device waits for an XMODEM transfer (128-byte packets). Received data is programmed sequentially starting at flash address 0x000000. The programmer updates an internal "Prog. CRC32" while writing.
  - Typical host workflow: start option 3 on the device, then from the host-side serial session send the ROM image with an XMODEM sender (for example `sx rom.bin` when using lrzsz inside the same serial terminal).

- 4 — Checksum Flash
  - Computes a CRC32 across the entire flash and prints the result. If a previous programming run produced a "Prog. CRC32", this option compares that value to the read-back CRC and reports verification success/failure.

- 5 — Read Byte
  - Prompts for a hex address (the prompt includes "0x"). Enter the address in hexadecimal (e.g. `0F1234`). The device reads and prints the single byte at that address.

- 6 — Program Byte
  - Prompts for a hex address and a hex data byte (both entered in hex). The single byte is programmed to the selected flash address. This uses the same program command sequence used for block programming (unlock + write).

- 7 — Read Flash (XMODEM upload)
  - Sends the entire flash contents back to the host using XMODEM. Useful to create a full dump of the cartridge. On the host, receive with an XMODEM receiver (for example `rx dump.bin` when using lrzsz).

- 0 — Erase, Program, and Verify Flash (XMODEM download)
  - Convenience sequence that runs: Erase Flash, Program Flash (XMODEM download) and then Checksum Flash (verify).

Notes and tips
- The firmware detects flash type/size using `getFlashID()`; ensure a valid flash is detected before programming to avoid incorrect size assumptions.

Example (host-side, when connected to the device serial terminal):

1. Program a ROM image (select menu option 3 on the device):

```sh
# from the same serial terminal that talks to the device, use an XMODEM sender
# (example using lrzsz)
sx rom.bin
```

2. Read a full dump from the device (select menu option 7 on the device):

```sh
# run this on the host-side serial terminal after selecting 7 on the device
rx cart_dump.bin
```
