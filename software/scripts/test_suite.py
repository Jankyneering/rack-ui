"""Interactive hardware test suite for the rack-ui board.

Walks through the full register map (version, config bits, encoder counters,
push button, LED brightness registers, GP RAM, soft reset) and cycles through
every animation. Each step that needs user action displays the live encoder
value and advances when the encoder push button is pressed and released.

Usage: python3 test_suite.py [i2cdriver_serial_port]
"""

import sys
import time

import i2cdriver

PORT = "/dev/tty.usbserial-DM02V7KY"
SLAVE_ADDR = 0x36

REG_FW_VERSION_HI = 0x00
REG_SOFT_RESET = 0x00
REG_FW_VERSION_LO = 0x01
REG_CONFIG = 0x02
REG_ENC_COUNT_HI = 0x03
REG_ENC_COUNT_LO = 0x04
REG_ENC_PUSH_COUNT = 0x05
REG_ENC_PUSH_STATE = 0x06
REG_ANIMATION = 0x0F
REG_LED_BASE = 0x10
REG_LED_COUNT = 12
REG_GP_BASE = 0x20

CFG_ROT_RESET_ON_READ = 1 << 0
CFG_PUSH_RESET_ON_READ = 1 << 1
CFG_ENC_DIR_FLIP = 1 << 2
CFG_PUSH_COUNT_DEC = 1 << 3
CFG_PUSH_STATE_FLIP = 1 << 4
CFG_LED_LINEAR = 1 << 5

FW_VERSION = [0, 1]

ANIMATIONS = [
    (0x00, "IDLE", "LEDs are driven manually from registers 0x10-0x1B"),
    (0x01, "LOADING", "rotating loading pattern (firmware default)"),
    (0x02, "BREATHING", "all LEDs fade in and out"),
    (0x03, "FOLLOWING", "every 4th LED lit, pattern follows rotation"),
    (0x04, "POINT", "single LED points at the rotation position"),
    (0x05, "GAUGE", "gauge fill level follows rotation, 0-100 (count resets on entry)"),
]

i2c = i2cdriver.I2CDriver(sys.argv[1] if len(sys.argv) > 1 else PORT)

failures = []


def set_reg(reg, value):
    i2c.start(SLAVE_ADDR, 0)
    i2c.write(bytes([reg, value]))
    i2c.stop()


def set_regs(reg, values):
    i2c.start(SLAVE_ADDR, 0)
    i2c.write(bytes([reg]) + bytes(values))
    i2c.stop()


def read_regs(reg, count):
    i2c.start(SLAVE_ADDR, 0)
    i2c.write(bytes([reg]))
    i2c.start(SLAVE_ADDR, 1)
    data = i2c.read(count)
    i2c.stop()
    return list(data)


def read_reg(reg):
    return read_regs(reg, 1)[0]


def signed16(data):
    value = (data[0] << 8) | data[1]
    return value - 0x10000 if value >= 0x8000 else value


def get_encoder_count():
    return signed16(read_regs(REG_ENC_COUNT_HI, 2))


def get_push_state():
    return read_reg(REG_ENC_PUSH_STATE)


def get_push_count():
    return read_reg(REG_ENC_PUSH_COUNT)


def check(condition, ok_msg, fail_msg):
    if condition:
        print(f"  PASS: {ok_msg}")
    else:
        print(f"  FAIL: {fail_msg}")
        failures.append(fail_msg)
    return condition


def wait_press_release(pressed_value=1, poll_s=0.02):
    while get_push_state() != pressed_value:
        time.sleep(poll_s)
    while get_push_state() == pressed_value:
        time.sleep(poll_s)


def live_encoder_display(note="", pressed_value=1, poll_s=0.02):
    """Show the live encoder rotation count and push state on one line until
    the push button is pressed and released. Returns the count at press time."""
    if note:
        print(f"  {note}")
    print("  (rotate the encoder to watch the value; press it to continue)")
    pressed = False
    count = 0
    while True:
        count = get_encoder_count()
        state = get_push_state()
        push_count = get_push_count()
        if state == pressed_value:
            pressed = True
        elif pressed:
            print(f"\r  rotation: {count:+6d}   push state: {state}   push count: {push_count:3d}   ")
            return count
        sys.stdout.write(
            f"\r  rotation: {count:+6d}   push state: {state}   push count: {push_count:3d}   "
        )
        sys.stdout.flush()
        time.sleep(poll_s)


def step_fw_version():
    version = read_regs(REG_FW_VERSION_HI, 2)
    print(f"  FW version registers 0x00-0x01: {version}")
    check(version == FW_VERSION, f"firmware version is {version}", f"unexpected firmware version {version} (expected {FW_VERSION})")


def step_gp_ram():
    pattern = [0xA5, 0x5A, 0x00, 0xFF]
    set_regs(REG_GP_BASE, pattern)
    readback = read_regs(REG_GP_BASE, len(pattern))
    print(f"  GP RAM 0x20 write {pattern}, read back {readback}")
    check(readback == pattern, "GP RAM readback matches", "GP RAM readback mismatch")

    set_reg(0x08, 0xFF)
    reserved = read_regs(0x08, 1)
    print(f"  reserved register 0x08 after writing 0xFF: {reserved}")
    check(reserved == [0x00], "reserved register write is ignored", f"reserved register 0x08 changed to {reserved}")

    set_reg(REG_ENC_PUSH_STATE, 0xFF)
    state = read_regs(REG_ENC_PUSH_STATE, 1)
    check(state in ([0x00], [0x01]), "read-only push state register ignores writes", f"push state register wrote {state}")


def step_led_registers():
    set_reg(REG_ANIMATION, 0x00)
    print("  Animation set to IDLE so the LED registers drive the LEDs directly")
    print("  LED chase: each of the 12 LEDs lights up in turn")
    for led in range(REG_LED_COUNT):
        set_regs(REG_LED_BASE, [63 if i == led else 0 for i in range(REG_LED_COUNT)])
        time.sleep(0.12)
    set_regs(REG_LED_BASE, [0] * REG_LED_COUNT)

    print("  Brightness ramp on all LEDs (0 to 63 and back)")
    for brightness in list(range(0, 64, 4)) + list(range(63, -1, -4)):
        set_regs(REG_LED_BASE, [brightness] * REG_LED_COUNT)
        time.sleep(0.03)
    set_regs(REG_LED_BASE, [0] * REG_LED_COUNT)
    readback = read_regs(REG_LED_BASE, REG_LED_COUNT)
    check(readback == [0] * REG_LED_COUNT, "LED registers read back as written", f"LED register readback {readback}")


def step_gamma_linear():
    set_reg(REG_ANIMATION, 0x00)
    set_reg(REG_CONFIG, 0x00)
    set_regs(REG_LED_BASE, [32] * REG_LED_COUNT)
    print("  All LEDs at register value 32, gamma-corrected (dimmer, perceptual)")
    wait_press_release()

    set_reg(REG_CONFIG, CFG_LED_LINEAR)
    print("  Same value 32 with CFG_LED_LINEAR (visually brighter, linear PWM)")
    wait_press_release()
    set_regs(REG_LED_BASE, [0] * REG_LED_COUNT)
    set_reg(REG_CONFIG, 0x00)


def step_encoder_live():
    count = live_encoder_display("Live encoder value, default config: rotation count increments clockwise")
    print(f"  rotation count at press: {count:+d}")


def step_rot_reset_on_read():
    set_reg(REG_CONFIG, CFG_ROT_RESET_ON_READ)
    print("  CFG_ROT_RESET_ON_READ set: the count clears to 0 after every read")
    print("  Keep rotating: the value jumps back to 0 each time it is read")
    live_encoder_display()
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")


def step_push_count():
    print("  Default mode: push count increments on every press (wraps past 255)")
    print("  Press the encoder button a few times and watch the count go up")
    live_encoder_display()
    count = get_push_count()
    check(count != 0, f"push count registered presses (now {count})", "push count never incremented")

    set_reg(REG_CONFIG, CFG_PUSH_RESET_ON_READ)
    print("  CFG_PUSH_RESET_ON_READ set: the count clears to 0 after every read")
    live_encoder_display()
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")


def step_push_count_dec():
    set_reg(REG_ENC_PUSH_COUNT, 5)
    set_reg(REG_CONFIG, CFG_PUSH_COUNT_DEC)
    print("  Push count preset to 5, CFG_PUSH_COUNT_DEC set: presses now count DOWN")
    print("  Press the encoder button and watch it decrement (saturates at 0)")
    live_encoder_display()
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")


def step_push_state_flip():
    set_reg(REG_CONFIG, CFG_PUSH_STATE_FLIP)
    print("  CFG_PUSH_STATE_FLIP set: state shows 1 while released and 0 while pressed")
    print("  The step ends on a press, so expect the display to read 0 right before it ends")
    live_encoder_display(pressed_value=0)
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")


def step_enc_dir_flip():
    set_reg(REG_CONFIG, CFG_ENC_DIR_FLIP)
    print("  CFG_ENC_DIR_FLIP set: rotation direction is inverted")
    print("  Rotate both ways: clockwise should now decrement the count")
    live_encoder_display()
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")


def step_animations():
    set_reg(REG_ENC_COUNT_HI, 0x00)
    set_reg(REG_ENC_COUNT_LO, 0x00)
    set_reg(REG_ANIMATION, 0x7F)
    readback = read_regs(REG_ANIMATION, 1)
    check(readback == [0x7F], "unknown animation id stored in register 0x0F", f"animation register readback {readback}")
    print("  Unknown animation falls back to IDLE (LEDs still follow registers 0x10-0x1B)")

    for anim_id, name, description in ANIMATIONS:
        print(f"\n  --- Animation 0x{anim_id:02X} {name}: {description}")
        set_reg(REG_ANIMATION, anim_id)
        readback = read_regs(REG_ANIMATION, 1)
        check(readback == [anim_id], f"animation {name} selected", f"animation register readback {readback} for {name}")
        live_encoder_display()


def step_soft_reset():
    set_regs(REG_GP_BASE, [0xA5])
    print("  Writing 0x01 to register 0x00 issues a soft reset")
    set_reg(REG_SOFT_RESET, 0x01)
    time.sleep(0.5)
    version = read_regs(REG_FW_VERSION_HI, 2)
    check(version == FW_VERSION, f"device back after reset (version {version})", f"no response after soft reset (version {version})")
    gp = read_regs(REG_GP_BASE, 1)
    check(gp == [0x00], "GP RAM back to power-on default 0x00", f"GP RAM after reset {gp}")
    anim = read_regs(REG_ANIMATION, 1)
    check(anim == [0x01], "animation back to default LOADING", f"animation after reset {anim}")
    config = read_regs(REG_CONFIG, 1)
    check(config == [0x00], "config back to default 0x00", f"config after reset {config}")


STEPS = [
    ("Firmware version", step_fw_version),
    ("General-purpose RAM and write protection", step_gp_ram),
    ("LED brightness registers (IDLE animation)", step_led_registers),
    ("Gamma vs linear brightness (CFG_LED_LINEAR)", step_gamma_linear),
    ("Encoder live value", step_encoder_live),
    ("Rotation reset-on-read (CFG_ROT_RESET_ON_READ)", step_rot_reset_on_read),
    ("Push button count (CFG_PUSH_RESET_ON_READ)", step_push_count),
    ("Push count decrement (CFG_PUSH_COUNT_DEC)", step_push_count_dec),
    ("Push state inversion (CFG_PUSH_STATE_FLIP)", step_push_state_flip),
    ("Rotation direction flip (CFG_ENC_DIR_FLIP)", step_enc_dir_flip),
    ("All animations", step_animations),
    ("Soft reset", step_soft_reset),
]


def main():
    print("rack-ui interactive test suite")
    print(f"I2C slave address: 0x{SLAVE_ADDR:02X}")
    try:
        for index, (title, func) in enumerate(STEPS, 1):
            print("\n" + "=" * 70)
            print(f" Step {index}/{len(STEPS)}: {title}")
            print("=" * 70)
            func()
    except KeyboardInterrupt:
        print("\nAborted by user")
    finally:
        try:
            set_reg(REG_CONFIG, 0x00)
            set_regs(REG_LED_BASE, [0] * REG_LED_COUNT)
        except Exception:
            pass
    print("\n" + "=" * 70)
    if failures:
        print(f" {len(failures)} check(s) failed:")
        for failure in failures:
            print(f"  - {failure}")
    else:
        print(" All checks passed")
    print("=" * 70)


if __name__ == "__main__":
    main()
