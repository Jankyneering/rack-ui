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
  Built-in animations (loading, flashing, pulsing, breathing, ...) can drive the array
  automatically; select one through register `0x0E` and adjust its setting (speed or
  brightness) through register `0x0F`, or set it to IDLE to control the LEDs manually.
  The multiplex/PWM refresh is advanced by a TIM16 update interrupt every 24 µs, giving a
  ~54 Hz full-array refresh that is flicker-free. The main loop sleeps in `__WFI()` between
  interrupts and applies register writes between them, so I2C transactions are never blocked
  by display updates.

## Hardware / pin map

| Function | Pin | Notes |
| --- | --- | --- |
| I2C SDA | PA2 | AF12, open-drain, pull-up, 100 kHz |
| I2C SCL | PA3 | AF12, open-drain, pull-up, 100 kHz |
| Encoder A | PA5 | EXTI edge(s) per `ENCODER_TRIGGER_TOGGLE`, triggers count update (1 ms glitch guard; lines are RC-debounced by 100 nF caps) |
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
| `0x00`-`0x01` | R/W(see note) | `0x01 0x00` | **Read**: firmware version, 16-bit big-endian (BCD-friendly: v1.0 reads `0x0100`; set through `FW_VERSION_MAJOR`/`FW_VERSION_MINOR` in the Makefile). **Write**: writing any non-zero value to `0x00` issues a soft reset (the version register is not writable; a zero write is ignored). |
| `0x02` | R/W | `0x00` | Configuration bits, see below. |
| `0x03`-`0x04` | R/W | `0x00 0x00` | Encoder rotation count, 16-bit big-endian **signed**. Incremented/decremented on rotation; cleared after the low byte is read if config bit 0 is set. |
| `0x05` | R/W | `0x00` | Encoder push button count, 8-bit unsigned. Updated on each debounced press; cleared after it is read if config bit 1 is set. |
| `0x06` | R/O | `0x00` | Encoder push button state: `0x01` while pressed (or `0x00` while pressed if config bit 4 is set). Sampled live when this register is transmitted. |
| `0x07`-`0x0D` | R/O | `0x00` | Reserved for future encoder settings. Reads return `0x00`; writes are ignored. |
| `0x0E` | R/W | `0xFE` | Active animation, see [below](#animation-register-0x0e). |
| `0x0F` | R/W | `0x3F` | Setting of the animation selected through `0x0E`, see [below](#animation-settings-register-0x0f). Writing a new animation id to `0x0E` reloads this register with that animation's default. |
| `0x10`-`0x1B` | R/W | `0x00` | LED brightness, one register per LED (LED 0 = `0x10` ... LED 11 = `0x1B`). `0x00` = off, `0x40` = full on; values above `0x40` clamp to full on. In IDLE mode (`0x0E` = `0x00`) these registers drive the LEDs directly; while an animation is running, the animation overwrites them. |
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

### Animation register (`0x0E`)

Selects the LED animation. New animations are added by extending the table in
`User/animations.c` (see `User/animations.h` for the ID enum); unknown values fall
back to `IDLE`.

| Value | Name | Description |
| --- | --- | --- |
| `0x00` | `IDLE` | Custom control: the LEDs are driven manually via registers `0x10`-`0x1B`. |
| `0x01` | `LOADING` | Rotating loading pattern, one LED at a time reaching full brightness. |
| `0x02` | `FLASHING` | All LEDs flash on and off. |
| `0x03` | `PULSING` | All LEDs smoothly ramp up and down in brightness. |
| `0x04` | `BREATHING` | All LEDs smoothly fade in, hold, fade out and pause. |
| `0x80` | `FOLLOWING` | Every 4th LED is lit and the pattern follows the encoder rotation (configurable with the `ANIMATION_FOLLOWING_LED_STEPS` constant). |
| `0x81` | `POINT` | One LED follows the encoder rotation. |
| `0x82` | `GAUGE` | The LEDs form a bar graph, with the number of lit LEDs proportional to the encoder rotation count. The encoder count is capped between 0 and 100. |
| `0xFE` | `ALL_ON` | All LEDs on at the brightness set through `0x0F`. Default to `CHARLIE_PWM_STEPS - 1`. |
| `0xFF` | `ALL_OFF` | All LEDs off. |

### Animation settings register (`0x0F`)

Holds the setting of the animation selected through register `0x0E` (timing
for LOADING/FLASHING/PULSING, brightness for ALL_ON and the off-state
brightness of the non-lit LEDs for FOLLOWING/POINT). Writing a new animation id
to `0x0E` loads that animation's default into `0x0F`; a subsequent write to
`0x0F` adjusts the setting while the animation keeps running. A value of `0` is
ignored and keeps the current setting.

| Animation | Scale | Default | Meaning |
| --- | --- | --- | --- |
| `IDLE` | - | `0x00` | No setting. |
| `LOADING` | x10 ms | `10` (100 ms) | Time between LED steps of the loading pattern. |
| `FLASHING` | x10 ms | `25` (250 ms) | Time between LED state toggles. |
| `PULSING` | x1 ms | `10` (10 ms) | Time between LED brightness changes. |
| `BREATHING` | - | `0x00` | Timing fixed by the `ANIMATION_BREATHING_*` constants; no runtime setting. |
| `FOLLOWING` | x brightness | `0` (off) | Brightness of the LEDs that are not lit. |
| `POINT` | x brightness | `0` (off) | Brightness of the LEDs that are not lit. |
| `GAUGE` | - | `0x00` | No runtime setting. |
| `ALL_ON` | x brightness | `63` (full on) | Brightness of all LEDs. |
| `ALL_OFF` | - | `0x00` | No setting. |

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
i2cset -y 1 0x36 0x0E 0x00
i2cset -y 1 0x36 0x10 0x10

# Switch to the pulsing animation; its default 10 ms pulse delay is loaded
# into register 0x0F
i2cset -y 1 0x36 0x0E 0x03

# Speed the pulsing animation up to a 5 ms delay
i2cset -y 1 0x36 0x0F 0x05

# Switch to the loading animation (100 ms default step time), then slow the
# rotation to 500 ms per step
i2cset -y 1 0x36 0x0E 0x01
i2cset -y 1 0x36 0x0F 0x32

# Turn all LEDs on at the default full brightness, then dim them to half
i2cset -y 1 0x36 0x0E 0xFE
i2cset -y 1 0x36 0x0F 0x20

# Turn all LEDs off
i2cset -y 1 0x36 0x0E 0xFF

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

From the `firmware/src` directory:

```bash
make ARM_TOOLCHAIN=/usr/bin        # or point ARM_TOOLCHAIN at your toolchain's bin/ directory
```

Useful variants:

```bash
make clean                        # remove build products
V=1 make                          # verbose output (prints full command lines)
make echo-version                 # print the firmware version (MAJOR.MINOR)
```

The firmware version is defined in the Makefile through `FW_VERSION_MAJOR` /
`FW_VERSION_MINOR` (default 1.0); it is baked into registers `0x00`-`0x01` and
names the build products. Override it for a local build with:

```bash
make ARM_TOOLCHAIN=/usr/bin FW_VERSION_MAJOR=2 FW_VERSION_MINOR=3
```

Encoder build flags in `User/main.h`:

- `ENCODER_TRIGGER_TOGGLE` (undefined by default) - define it to count both EncA
  edges (one tick per detent on encoders that produce one edge per detent); leave it
  commented out to count the falling edge only
- `ENCODER_DIRECTION_FLIP` (undefined by default) - define it to make clockwise
  rotation decrement the count instead of incrementing, for encoder models with
  reversed phase wiring; composes with the runtime `CFG_ENC_DIR_FLIP` config bit
- `ENCODER_AB_SWAP` (defined) - swap the phase inputs
  (EncA moves to PA4, EncB to PA5) for encoder models whose trigger output sits on
  the other phase or whose A/B pins are wired the other way round; note that
  swapping the phases also inverts the decoded direction, so combine it with
  `ENCODER_DIRECTION_FLIP` to keep the increment direction

Output files land in `firmware/src/Build/`, named after the app and version
(`mu-cell_rack-ui_vMAJOR.MINOR`):

| File | Description |
| --- | --- |
| `mu-cell_rack-ui_vX.Y.bin` | Raw binary image, for flashing |
| `mu-cell_rack-ui_vX.Y.hex` | Intel HEX image |
| `mu-cell_rack-ui_vX.Y.elf` | ELF with debug symbols |
| `mu-cell_rack-ui_vX.Y.lst` | Disassembly listing |

### 3. Flash

The Makefile supports flashing via **PyOCD** (DAPLink or J-Link probes) or **J-Link**,
selected with `FLASH_PROGRM`:

```bash
make ARM_TOOLCHAIN=/usr/bin flash          # uses pyocd (default)
make ARM_TOOLCHAIN=/usr/bin FLASH_PROGRM=jlink flash
```

Refer to the [py32f0-template wiki](https://github.com/IOsetting/py32f0-template/wiki)
for detailed tooling setup (PyOCD, J-Link, VS Code debugging).

## CI and releases

The [firmware workflow](../.github/workflows/firmware.yml) builds the firmware on every push
to `main`, on pull requests and on `vX.Y` tags, uploading the build outputs as an artifact.
On each push to `main`, a **nightly** rolling release is published containing the latest
`.bin` (plus `.hex` and `.elf`), named `mu-cell_rack-ui_nightly-<commit>` with the commit it
was built from. Pushing a `vX.Y` tag instead publishes a versioned release: the firmware is
built with the tagged version and the assets are named `mu-cell_rack-ui_vX.Y.bin/.elf/.hex`.
You can grab prebuilt binaries from the repository's
[releases page](https://github.com/Jankyneering/rack-ui/releases) without installing a
toolchain.

## License & Acknowledgements

* PY32 Template from [IOsetting](https://github.com/IOsetting/py32f0-template)

Published under CreativeCommons BY-SA 4.0

[![Creative Commons License](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-sa/4.0/)

This work is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-sa/4.0/).
