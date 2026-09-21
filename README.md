# Rack UI

## I2C Register Map

The device is an I2C slave at address `0x36` (7-bit). Registers are one byte each; write the register pointer, then read/write the data.

| Register | Name             | Access | Description                                                                                          |
| -------- | ---------------- | ------ | ---------------------------------------------------------------------------------------------------- |
| `0x00`   | FW version hi    | R/O    | Firmware version, high byte (big-endian). Writing a non-zero value triggers a soft reset.            |
| `0x01`   | FW version lo    | R/O    | Firmware version, low byte.                                                                          |
| `0x02`   | Config           | R/W    | Configuration bits, see below.                                                                       |
| `0x03`   | Enc count hi     | R/W    | Encoder rotation count, high byte.                                                                   |
| `0x04`   | Enc count lo     | R/W    | Encoder rotation count, low byte.                                                                     |
| `0x05`   | Enc push count   | R/W    | Encoder push button count.                                                                           |
| `0x06`   | Enc push state   | R/O    | Encoder push button state (1 = pushed).                                                              |
| `0x07`-`0x0E` | Reserved     | —      | Reserved for future encoder settings.                                                                |
| `0x0F`   | Animation        | R/W    | Active animation, see below.                                                                          |
| `0x10`-`0x1B` | LED brightness | R/W   | Per-LED brightness (`0`–`64`), one register per LED. Applies when the animation is IDLE.                |
| `0x1C`-`0x1F` | Reserved     | —      | Reserved.                                                                                              |
| `0x20`-`0xFF` | GP RAM        | R/W    | General-purpose I2C RAM.                                                                              |

### Config register (0x02) bits

| Bit | Name                  | Effect                                                                   |
| --- | --------------------- | ------------------------------------------------------------------------ |
| 0   | `CFG_ROT_RESET_ON_READ`  | 1: reset rotation count after its low byte is read                    |
| 1   | `CFG_PUSH_RESET_ON_READ` | 1: reset push count after it is read                                 |
| 2   | `CFG_ENC_DIR_FLIP`       | 1: flip encoder increment direction                                  |
| 3   | `CFG_PUSH_COUNT_DEC`     | 1: decrement push count on press, 0: increment                       |
| 4   | `CFG_PUSH_STATE_FLIP`    | 1: invert reported push button state                                 |
| 5   | `CFG_LED_LINEAR`         | 1: linear LED brightness, 0: gamma-corrected                          |

Bits 6–7 are reserved and always read 0.

### Animation register (0x0F)

Selects the active LED animation. Unknown values fall back to IDLE. New animations are added in [`software/src/User/animations.c`](software/src/User/animations.c).

| Value    | Name       | Description                                                       |
| -------- | ---------- | ----------------------------------------------------------------- |
| `0x00`   | `IDLE`     | Custom control: the LEDs are driven manually via registers `0x10`-`0x1B` |
| `0x01`   | `LOADING`  | Rotating loading animation (default at boot)                       |
| `0x02`   | `BREATHING`| All LEDs smoothly fade in and out                                 |

## License & Acknowledgements

- PY32 Template from [IOsetting](https://github.com/IOsetting/py32f0-template)

Made with ❤️, lots of ☕️, and lack of 🛌  
Published under CreativeCommons BY-SA 4.0

[![Creative Commons License](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-sa/4.0/)  
This work is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-sa/4.0/).
