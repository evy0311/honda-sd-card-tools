# Honda SD Card Tools

This project contains Teensy firmware and host scripts for interacting with locked Honda SD cards at the raw block level. It is primarily intended for inspection, backup, and research of SD cards that use the SD CMD42 password lock feature.

The firmware runs on a Teensy 4.1 using SPI mode and the danman fork of the Arduino SD library. Communication happens over USB serial to an SD card breakout board. The following was used from SparkFun Electronics: [SparkFun SD Sniffer](https://www.sparkfun.com/sd-sniffer.html)

## Features

- Initialize SD cards over SPI
- Read and print the card CID
- Preview sector 0
- Unlock, set, and clear SD passwords using CMD42
- Full card dump with metadata header
- Byte exact raw card dump with zero framing or logging
- CRC32 hashing of arbitrary LBA ranges for verification

## Hardware Requirements

- Teensy 4.1
- SD card wired in SPI mode
- Level safe 3.3V power

### SPI Wiring

| SD Signal | Teensy Pin |
|---------|------------|
| DAT3 CS | 10 |
| CMD MOSI | 11 |
| DAT0 MISO | 12 |
| CLK SCK | 13 |
| 3.3V | 3.3V |
| GND | GND |

## Firmware

The main firmware lives in `src/main.cpp`.

After flashing, open a serial monitor at 115200 baud to access the command menu.

### Serial Commands

| Key | Description |
|---|---|
| i | Initialize card |
| c | Print CID |
| b | Preview first 64 bytes of block 0 |
| u | CMD42 unlock using stored password |
| l | Set password and lock card |
| x | Clear password |
| d | Dump full card with header and logs |
| R | Raw dump, exact bytes only |
| n | Return block count as 4 little endian bytes |
| H | CRC32 over supplied LBA range |
| K | CRC32 over hard coded range |
| h or ? | Show help |

## Dump Modes

There are two different dump mechanisms. Which one you use depends on whether you want a human readable protocol or a byte exact disk image.

### Dump (Command `d`)

This is a verbose dump mode intended for interactive use or custom host scripts.

Protocol:

- ASCII header `STARTIMG\n`
- 4 bytes little endian block count
- Raw block data
- Periodic progress text over serial

This mode is useful when debugging or when you want the firmware to tolerate read errors by zero filling failed sectors while continuing the dump.

Because it includes text output, it is not byte exact unless your host script explicitly parses and strips the header and logs.

### Dump Raw (Command `R`)

This is the recommended mode for forensic quality backups.

Characteristics:

- No ASCII text
- No headers
- No progress output
- Exactly blocks × 512 bytes streamed
- Read failures are replaced with zero filled sectors to preserve alignment

This mode is designed to be captured by a host program that knows the card size in advance and reads an exact byte count.

## dump_raw.py

The included `dump_raw.py` script performs a byte exact capture of the SD card image.

### Requirements

- Python 3
- pyserial

Install dependency:
pip install pyserial

### Usage

python3 dump_raw.py <serial_port> <output_image>

Example:
python3 dump_raw.py /dev/tty.usbmodem123456 dump_raw.img

### How It Works

1. Opens the serial port at 115200 baud
2. Sends `i` to initialize the card
3. Sends `n` to receive the 4 byte little endian block count
4. Calculates expected total byte size
5. Sends `R` to begin raw streaming
6. Reads exactly block_count × 512 bytes
7. Writes data directly to disk
8. Performs a sanity check for a 0x55AA boot sector signature

If the serial stream stalls or returns fewer bytes than expected, the script aborts to prevent silent corruption.

## Safety Notes

- CMD42 password operations can permanently lock SD cards if misused
- Always verify the password bytes before issuing SET or CLEAR commands
- Do not remove power or the SD card during a dump
- Raw dumps can take several minutes depending on card size and USB speed

## Intended Use

This project is intended for research, reverse engineering, and personal backup of SD cards you own. It is not intended for bypassing access controls on devices you do not have legal rights to.

## License

Use at your own risk. No warranty is provided.