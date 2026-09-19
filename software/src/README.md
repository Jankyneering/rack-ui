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
  software PWM and gamma correction (curve exponent 2.2, built into a lookup table at boot).
  The multiplex/PWM refresh is advanced by a TIM16 update interrupt every 24 µs, giving a
  ~54 Hz full-array refresh that is flicker-free. The main loop sleeps in `__WFI()` between
  interrupts.
* **Demo behaviour** - out of the box, the LED array runs a "breathing" fade ramp (updated
  every 50 ms) and register `0x01` controls the onboard status LED.

## Hardware / pin map

| Function | Pin | Notes |
| --- | --- | --- |
| I2C SDA | PA2 | AF12, open-drain, pull-up, 100 kHz |
| I2C SCL | PA3 | AF12, open-drain, pull-up, 100 kHz |
| Encoder A | PA5 | EXTI falling edge, triggers count update (50 ms debounce) |
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
| `0x00` | R/W | `0xAA` | Status/demo register. Firmware writes `0x55` here while register `0x01` is `0xFF`, otherwise `0xAA`. |
| `0x01` | R/W | `0x0D` | Onboard LED control (demo). Write `0xFF` to turn the PB5 LED **on**, any other value turns it **off**. |
| `0x02` | R/W | `0x0E` | Free R/W register. |
| `0x03` | R/W | `0x0F` | Free R/W register. |
| `0x04` | R/O | `0x00` | Encoder count, high byte (16-bit, big-endian). |
| `0x05` | R/O | `0x00` | Encoder count, low byte. Incremented/decremented on rotation. |
| `0x06` | R/O | `0x00` | Encoder switch state, bit 0 = `1` while pressed. Sampled at the start of each read transaction. |
| `0x07` | R/O | `0x00` | Debounced button press counter. Incremented on each press; cleared automatically after being read. |
| `0x08`-`0xFF` | R/O | `0x42` | Unimplemented. Reads return `0x42`; writes are ignored. |

Notes:

* Only registers `0x00`-`0x03` can be written by the master.
* The encoder counter is stored big-endian, so read `0x04` then `0x05` (with auto-increment)
  to get a coherent 16-bit value.

### Examples (Linux `i2c-tools`)

```bash
# Turn the onboard LED on
i2cset -y 1 0x36 0x01 0xFF

# Read the 16-bit encoder count
i2ctransfer -y 1 w1@0x36 0x04 r2

# Read the button state (bit 0) and press counter
i2cget -y 1 0x36 0x06
i2cget -y 1 0x36 0x07
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
