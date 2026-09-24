---
title: Installation
nav_order: 2
---

# Installation

## Supported Devices

Official CrossCover firmware is built and hardware-tested only for:

- Xteink X3
- Xteink X4

Do not flash CrossCover on a USB-locked Xteink device.

The source tree retains CrossInk environments for Sticky, X4 Pro, and X4
Classic, but the CrossCover maintainer does not own those devices. CrossCover
does not publish or support firmware images for them. Advanced users can read
[Building Unofficial Device Targets](./development/unsupported-target-builds.md)
and compile them at their own risk.

## Download

Download `firmware-x3-x4-*.bin` from the
[CrossCover releases page](https://github.com/StefanTsonev/CrossCover/releases).
Verify that the release identifies the expected CrossCover version before
flashing.

## SD Card Firmware Update

This method is for a supported device already running firmware that provides
the SD update screen.

1. Copy the downloaded `firmware-x3-x4-*.bin` file to the SD card.
2. Open `Settings > System > SD Card Firmware Update`.
3. Select the firmware file and confirm the update.

Back up important SD-card files first and keep the device powered until the
update completes.

## Command Line

Install `esptool`:

```sh
pip3 install esptool
```

Connect the X3 or X4 by USB-C after downloading the image from the CrossCover
releases page.

Find the device port:

```sh
# Linux
dmesg | grep tty

# macOS
ls /dev/cu.*
```

Flash the firmware:

```sh
# Linux
esptool.py --chip esp32c3 --port /dev/ttyACM0 --baud 921600 write_flash 0x10000 /path/to/firmware.bin

# macOS
esptool.py --chip esp32c3 --port /dev/cu.usbmodem2101 --baud 921600 write_flash 0x10000 /path/to/firmware.bin
```

Replace the port and firmware path with your actual values.
