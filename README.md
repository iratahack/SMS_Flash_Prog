# SEGA Master System Flash Cartridge Programmer

## Arduino COM Port Setup

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
