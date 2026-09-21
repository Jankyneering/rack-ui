# Rack UI Firmware

Firmware for the [Rack UI](../README.md) front panel controller board: a Puya **PY32F002Ax5**
(Arm Cortex-M0+) that acts as an **I2C peripheral**, exposing a rotary encoder, a push button and
a charlieplexed LED array to a host over a single I2C bus.

The firmware is built on the [py32f0-template](https://github.com/IOsetting/py32f0-template)
project using the Puya LL (Low Layer) driver library.

## What the firmware does

* **Encoder-to-I2C converter** - rotary encoder position and button presses are captured by
  interrupt and exposed through an I2C register map (see below), so a host (e.g. a Raspberry
  Pi or another MCU) can poll the panel without GPIO wiring.
* **LED driving** - 12 LEDs are driven from 4 GPIO pins by charlieplexing, with 64-step
  software PWM and optional gamma correction (curve exponent 2.2, built into a lookup table
  at boot; toggle via config bit 5). Per-LED brightness is set through registers `0x10`-`0x1B`.
  Built-in animations (rotating loading pattern, breathing) can drive the array
  automatically; select one through register `0x0F`, or set it to IDLE to control the
  LEDs manually.
  The multiplex/PWM refresh is advanced by a TIM16 update interrupt every 24 µs, giving a
  ~54 Hz full-array refresh that is flicker-free. The main loop sleeps in `__WFI()` between
  interrupts and applies register writes between them, so I2C transactions are never blocked
  by display updates.

## Hardware / pin map

| Function | Pin | Notes |
| --- | --- | --- |
| I2C SDA | PA2 | AF12, open-drain, pull-up, 100 kHz |
| I2C SCL | PA3 | AF12, open-drain, pull-up, 100 kHz |
| Encoder A | PA5 | EXTI edge(s) per `ENCODER_TRIGGER_TOGGLE`, triggers count update (50 ms debounce) |
| Encoder B | PA4 | Input, sampled to determine rotation direction |
| Encoder switch | PA0 | EXTI both edges, debounced press counter (50 ms) |
| LED charlieplex X0 | PA6 | 12 LEDs across 4 pins |
| LED charlieplex X1 | PA7 | |
| LED charlieplex X2 | PA12 | |
| LED charlieplex X3 | PA8 | |
| Onboard LED | PB5 | Push-pull output, **active low** |

The system clock runs from the internal 8 MHz HSI. Interrupt priorities are
I2C (0) > EXTI (1) > TIM16 refresh (2), so host communication and encoder input are never
starved by the display refresh.

## I2C interface

* **Address**: `0x36` (7-bit)
* **Speed**: 100 kHz
* **Protocol**: standard register-pointer style. To read or write, first send the register
  address in a write transaction, then read or write data in the same or a following
  transaction. The register pointer auto-increments for multi-byte transfers.

### Register map

| Register | Access | Default | Description |
| --- | --- | --- | --- |
| `0x00`-`0x01` | R/W(see note) | `0x00 0x01` | **Read**: firmware version, 16-bit big-endian (BCD-friendly: v0.1 reads `0x0001`). **Write**: writing any non-zero value to `0x00` issues a soft reset (the version register is not writable; a zero write is ignored). |
| `0x02` | R/W | `0x00` | Configuration bits, see below. |
| `0x03`-`0x04` | R/W | `0x00 0x00` | Encoder rotation count, 16-bit big-endian **signed**. Incremented/decremented on rotation; cleared after the low byte is read if config bit 0 is set. |
| `0x05` | R/W | `0x00` | Encoder push button count, 8-bit unsigned. Updated on each debounced press; cleared after it is read if config bit 1 is set. |
| `0x06` | R/O | `0x00` | Encoder push button state: `0x01` while pressed (or `0x00` while pressed if config bit 4 is set). Sampled live when this register is transmitted. |
| `0x07`-`0x0E` | R/O | `0x00` | Reserved for future encoder settings. Reads return `0x00`; writes are ignored. |
| `0x0F` | R/W | `0x01` | Active animation, see [below](#animation-register-0x0f). |
| `0x10`-`0x1B` | R/W | `0x00` | LED brightness, one register per LED (LED 0 = `0x10` ... LED 11 = `0x1B`). `0x00` = off, `0x40` = full on; values above `0x40` clamp to full on. In IDLE mode (`0x0F` = `0x00`) these registers drive the LEDs directly; while an animation is running, the animation overwrites them. |
| `0x1C`-`0x1F` | R/O | `0x00` | Reserved. Reads return `0x00`; writes are ignored. |
| `0x20`-`0xFF` | R/W | `0x00` | General-purpose I2C RAM. Not used by the firmware; usable as 224 bytes of host scratch space. |

### Config register (`0x02`)

| Bit | Default | Description |
| --- | --- | --- |
| 0 | `0` | `1` = clear the rotation count (`0x03`-`0x04`) to zero after the low byte (`0x04`) is read. `0` (default) = keep the count. |
| 1 | `0` | `1` = clear the push count (`0x05`) to zero after it is read. `0` (default) = keep the count. |
| 2 | `0` | `1` = flip encoder increment direction. |
| 3 | `0` | `1` = decrement push count on press (saturates at 0, e.g. for "count down remaining presses" logic), `0` = increment (wraps past 255). |
| 4 | `0` | `1` = invert the reported push button state: unpressed reads `1`, pressed reads `0`. |
| 5 | `0` | `0` = gamma-correct LED brightness (default), `1` = linear brightness. |
| 6-7 | `0` | Reserved, always read 0. |

Notes:

* Writes to read-only registers are ignored (the register pointer still advances, so
  multi-byte writes can skip over them).
* A soft reset completes the current I2C transaction first, then resets the MCU; the bus
  release means the master sees a normal STOP rather than a stuck line. After reset, all
  registers return to their power-on defaults.
* The rotation counter is big-endian; read `0x03` then `0x04` in one transaction for a
  coherent value.
* Register defaults are reinitialised only at power-on/reset; the general-purpose RAM is not
  preserved across resets.

### Animation register (`0x0F`)

Selects the LED animation. New animations are added by extending the table in
`User/animations.c` (see `User/animations.h` for the ID enum); unknown values fall
back to `IDLE`.

| Value | Name | Description |
| --- | --- | --- |
| `0x00` | `IDLE` | Custom control: the LEDs are driven manually via registers `0x10`-`0x1B`. |
| `0x01` | `LOADING` | Rotating loading pattern, one LED at a time reaching full brightness. **Default at power-on.** |
| `0x02` | `BREATHING` | All LEDs smoothly fade in, hold, fade out and pause. |

Animations write their brightness values to the LED registers (`0x10`-`0x1B`) and
mark them dirty, so the main loop applies them exactly like host writes. The
animation engine ticks every 11 ms from the main loop.

### Examples (Linux `i2c-tools`)

```bash
# Read the firmware version (expects 0x00 0x01)
i2ctransfer -y 1 w1@0x36 0x00 r2

# Read the 16-bit encoder rotation count
i2ctransfer -y 1 w1@0x36 0x03 r2

# Read the button state and push count
i2cget -y 1 0x36 0x06
i2cget -y 1 0x36 0x05

# Enable reset-on-read for the rotation count and push count
i2cset -y 1 0x36 0x02 0x03

# Soft-reset the device (registers return to power-on defaults)
i2cset -y 1 0x36 0x00 0x01

# Set LED 0 to quarter brightness and LED 11 to full, gamma-corrected
i2cset -y 1 0x36 0x10 0x10
i2cset -y 1 0x36 0x1B 0x40

# Set all 12 LED brightness registers in one transaction
i2ctransfer -y 1 w13@0x36 0x10 0x40 0x40 0x40 0x40 0x40 0x40 0x40 0x40 0x40 0x40 0x40 0x40

# Flip encoder direction and switch LEDs to linear brightness
i2cset -y 1 0x36 0x02 0x24

# Switch to manual LED control (IDLE), then set LED 0 to quarter brightness
i2cset -y 1 0x36 0x0F 0x00
i2cset -y 1 0x36 0x10 0x10

# Switch to the breathing animation
i2cset -y 1 0x36 0x0F 0x02

# Use register 0x20 as scratch RAM
i2cset -y 1 0x36 0x20 0xA5
i2cget -y 1 0x36 0x20
```

## Building

### 1. Install the GNU Arm Embedded toolchain

Download the toolchain from
[Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
for your architecture and extract it, or install a distribution package:

```bash
# Debian/Ubuntu
sudo apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi
# -> toolchain in /usr/bin, set ARM_TOOLCHAIN=/usr/bin when building
```

### 2. Build

From the `software/src` directory:

```bash
make ARM_TOOLCHAIN=/usr/bin        # or point ARM_TOOLCHAIN at your toolchain's bin/ directory
```

Useful variants:

```bash
make clean                        # remove build products
V=1 make                          # verbose output (prints full command lines)
```

To count both EncA edges instead of the falling edge only (for encoders that produce
one edge per detent), define `ENCODER_TRIGGER_TOGGLE` in `User/main.h`; comment the
`#define` out to restore falling-edge-only counting.

Output files land in `software/src/Build/`:

| File | Description |
| --- | --- |
| `app.bin` | Raw binary image, for flashing |
| `app.hex` | Intel HEX image |
| `app.elf` | ELF with debug symbols |
| `app.lst` | Disassembly listing |

### 3. Flash

The Makefile supports flashing via **PyOCD** (DAPLink or J-Link probes) or **J-Link**,
selected with `FLASH_PROGRM`:

```bash
make ARM_TOOLCHAIN=/usr/bin flash          # uses pyocd (default)
make ARM_TOOLCHAIN=/usr/bin FLASH_PROGRM=jlink flash
```

Refer to the [py32f0-template wiki](https://github.com/IOsetting/py32f0-template/wiki)
for detailed tooling setup (PyOCD, J-Link, VS Code debugging).

## CI and nightly builds

The [firmware workflow](../.github/workflows/firmware.yml) builds the firmware on every push
to `main` and on pull requests, uploading the build outputs as an artifact. On each push to
`main`, a **nightly** rolling release is published containing the latest `app.bin` (plus
`.hex` and `.elf`), named with the commit it was built from. You can grab prebuilt binaries
from the repository's [releases page](https://github.com/Jankyneering/rack-ui/releases)
without installing a toolchain.

## License & Acknowledgements

* PY32 Template from [IOsetting](https://github.com/IOsetting/py32f0-template)

Published under CreativeCommons BY-SA 4.0

[![Creative Commons License](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-sa/4.0/)

This work is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-sa/4.0/).
