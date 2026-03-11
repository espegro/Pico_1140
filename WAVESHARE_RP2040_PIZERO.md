# Pico_1140 for Waveshare RP2040-PiZero

This document describes the modifications needed to run Pico_1140 on the **Waveshare RP2040-PiZero** board.

## Hardware Differences

### Waveshare RP2040-PiZero vs SparkFun Thing Plus
- **SD Card SPI pins:** Different from SparkFun board
- **UART pins:** Different GPIO mapping
- **Single USB CDC:** Unlike SparkFun which uses dual CDC ports

### Pin Configuration (Waveshare RP2040-PiZero)
- **SPI0 (SD Card):**
  - SCK: GPIO 18
  - MOSI: GPIO 19
  - MISO: GPIO 20
  - CS: GPIO 21
- **UART0:** GPIO 0 (TX), GPIO 1 (RX) - used by stdio
- **UART1:** GPIO 4 (TX), GPIO 5 (RX) - available for DL11

## Required Changes

### 1. hw_config.c
Updated SPI pin configuration for Waveshare SD card interface:
```c
.hw_inst = spi0,
.miso_gpio = 20,  // Waveshare RP2040-PiZero
.mosi_gpio = 19,  // Waveshare RP2040-PiZero
.sck_gpio = 18,   // Waveshare RP2040-PiZero
.ss_gpio = 21,    // Waveshare RP2040-PiZero
```

### 2. getline.cxx
Fixed USB input blocking issue:
```c
// Changed from busy_wait_ms() to sleep_ms() + tud_task()
sleep_ms(10);     // Allow interrupts
tud_task();       // Process TinyUSB tasks
```

**Reason:** `busy_wait_ms()` blocks all interrupts, preventing USB CDC from functioning.

### 3. avr11.cxx
Critical performance and compatibility fixes:

#### Polling Frequency (CRITICAL FIX)
```c
// Changed from every 2,000 steps to 50,000 steps
if (kbdelay++ == 50000) {  // Was: 2000
    tud_task();  // Process USB before polling
    cpu.unibus.cons.poll();
    // dl11.poll() disabled - see below
    kbdelay = 0;
}
```

**Reason:**
- Original polling every 2,000 steps caused significant overhead
- On Waveshare, `dl11.poll()` with USB CDC blocks the entire emulator
- Solution: Poll 25x less frequently, disable dl11.poll()

#### DL11 Disabled
```c
// cpu.unibus.dl11.poll();  // DISABLED - causes blocking
```

**Impact:**
- ✅ Single-user Unix V6 works perfectly
- ✅ RT11, MINC BASIC work fine
- ❌ Multi-user support not available (requires second serial port)

**Future Fix:** Could enable DL11 using hardware UART1 (GPIO 4/5) instead of USB CDC.

### 4. Pico_1140.cxx
Added missing header:
```c
#include "hardware/clocks.h"  // For set_sys_clock_khz()
```

Fixed file opening logic to handle empty filenames:
```c
if (rkfile[0] != '\0') {  // Check before opening
    fr = f_open(&cpu.unibus.rk11.rk05, rkfile, FA_READ | FA_WRITE);
    // ...
}
```

## Performance

### Original Implementation
- Polling: Every 2,000 CPU steps
- Both cons.poll() and dl11.poll() active

### Optimized for Waveshare
- Polling: Every 50,000 CPU steps (25x less frequent)
- Only cons.poll() active
- tud_task() called before polling for USB input

### Expected Performance
**Faster than original** due to:
- 25x less polling overhead
- No blocking dl11.poll()
- Efficient USB handling

Should achieve **significantly faster than real PDP-11/40 hardware** (which ran at ~0.3 MIPS).

## What Works
✅ Single-user Unix V6
✅ RT11 (single user)
✅ MINC BASIC
✅ Boot menu and disk selection
✅ Keyboard input via USB
✅ Console output via USB
✅ SD card (FAT32, tested with 32GB)

## What Doesn't Work
❌ Multi-user Unix V6 (requires DL11)
❌ Multi-user BASIC
❌ Multi-user RSTS/E
❌ Second serial port (dl11.poll disabled)

## Building

Same as original, but ensure:
1. Correct SPI pins in `hw_config.c` (see above)
2. `dl11.poll()` remains disabled in `avr11.cxx`
3. Polling frequency set to 50,000 steps

```bash
cd Pico_1140_DC
mkdir build && cd build
cmake ..
make -j4
```

Copy `Pico_1140.uf2` to Waveshare RP2040-PiZero in bootloader mode.

## Troubleshooting

### No output after "Ready"
- Check that `dl11.poll()` is disabled
- Verify `tud_task()` is called before `cons.poll()`

### No keyboard input
- Ensure `getline.cxx` uses `sleep_ms()` + `tud_task()` instead of `busy_wait_ms()`

### SD card not mounting
- Verify SPI pins match Waveshare RP2040-PiZero pinout
- Check SD card is FAT32 formatted

## Credits

Original Pico_1140 by Ian Schofield
Waveshare RP2040-PiZero port by Espen (March 2026)

## License

Same as original Pico_1140 project.
