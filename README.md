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

## Hardware Interface

The programmer accesses the SMS cartridge slot using the following bus signals:

- **Address Bus (A0-A15)**: Driven by SPI-controlled 74HC595 shift registers
- **Data Bus (D0-D7)**: Bidirectional GPIO on Arduino pins D2-D9
- **Control Signals**:
  - _CE (Chip Enable): PC1
  - _RD (Read Enable): PC2
  - _WR (Write Enable): PC3
  - RCLK (Register Clock for address latching): PC0

The programmer supports dual cartridge mappers (SEGA standard and Iratahack), automatically detecting the mapper type at boot and allowing manual switching via the menu.

## Usage

The firmware presents a serial menu interface for ROM programming and verification. Connect via serial terminal at 2000000 baud to interact with the device.

Example workflow (host-side, when connected to the device serial terminal):

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
