# Pico_1140 on TENSTAR RP2350-USB 16MB

This port runs the PDP-11/40 emulator without an SD card. It builds for the
TENSTAR RP2350-USB 16 MB board using the electrically compatible
`waveshare_rp2350_plus_16mb` Pico SDK profile, and bundles `Unix_V6.RK05` in
the UF2 image.

## Flash layout

| Flash offset | Size | Use |
| --- | ---: | --- |
| `0x000000` | 1 MB maximum | RP2350 firmware |
| `0x100000` | about 2 MB | Immutable Unix V6 RK05 base image |
| `0x300000` | 1 MB | Reserved |
| `0x400000` | 6 MB | Writable journal bank A |
| `0xA00000` | 6 MB | Writable journal bank B |

The RK05 is exposed as a conventional 2,494,464-byte disk (4,872 sectors).
The bundled image
is shorter; unread tail sectors initially contain zeroes.

PDP-11 writes do not modify the base image. Each changed 512-byte sector is
appended to the active journal bank with CRC-protected metadata. At startup the
latest valid copy of every sector is indexed in RAM. When a bank fills, live
sectors are copied to the other bank and that bank is committed atomically.
An interrupted compaction therefore leaves the previous bank usable.

Reflashing this UF2 preserves an existing journal when its image size and CRC
match. A full-chip erase resets the RK05 to the bundled Unix V6 image.

## Build

Pico SDK 2.2.0 or newer and an Arm embedded GCC toolchain are recommended.
The board defaults to Waveshare RP2350-Plus 16MB, so no board argument is
required.

```sh
cmake -S Pico_1140_RP2350_USB -B build-rp2350 \
  -DPICO_SDK_PATH=/path/to/pico-sdk \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-rp2350 -j
```

The output is `build-rp2350/Pico_1140.uf2`.

## First boot

The first boot formats one 6 MB journal bank and can take a while. Connect a
terminal to the USB CDC port. The firmware prints the disk initialization
status and then boots the RK05 bootstrap. Unix V6 normally presents an `@`
prompt; enter `unix` to start the operating system.

Unix V6 accepts normal lowercase input. Logging in as `ROOT` in capitals can
activate its historical uppercase-only terminal compatibility mode; use
lowercase `root` for normal terminal behavior. The emulator passes 7-bit ASCII
through unchanged and does not force either case.

The console is also mirrored to UART0 on GPIO 0/1 at 9600 baud. UART1 uses
GPIO 20/21, although the emulator's second DL11 polling remains disabled in
this port.

## RGB status LED

The TENSTAR RP2350-USB board's single WS2812-compatible LED is driven by PIO
on GPIO 22. The colors are deliberately kept at low brightness:

- cyan flashes: firmware startup
- amber: internal RK05 initialization
- green: emulator running and disk idle
- blue: RK05 read activity
- purple: RK05 write activity
- red: internal disk initialization failure

## Current limitations

- Only the bundled RK05 drive is configured. RL01/RL02 selection is disabled.
- The binary tape loader and SD-card menu are disabled.
- The tested board is TENSTAR RP2350-USB 16 MB with its WS2812 LED on GPIO 22.
- Hardware retesting is required after changing the journal format or flash
  partition offsets.
- A sudden power loss can discard the sector currently being written, but a
  partially written record is rejected by its CRC and cannot replace an older
  valid sector.
