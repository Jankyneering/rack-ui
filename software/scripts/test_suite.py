"""Interactive hardware test suite for the rack-ui board.

Walks through the full register map (version, config bits, encoder counters,
push button, LED brightness registers, GP RAM, soft reset) and cycles through
every animation. Each step that needs user action displays the live encoder
value and advances when the encoder push button is pressed and released.
During the GAUGE animation of the animation cycle, the maximum range
(register 0x0F) is verified automatically: the suite writes encoder counts
above, below and inside the configured range and checks the clamping and the
LED bar levels against the firmware headers, for the default range, then 0-10
and 0-255.

When the optional SSD1306 128x64 OLED is fitted, every step is mirrored on it:
the top 16 pixel rows (the yellow band on two-color modules) show a title bar
with the step counter, and the remaining area shows live values and PASS/FAIL
results. Without a display the suite still runs, console only.

Usage: python3 test_suite.py [i2cdriver_serial_port]
"""

import os
import sys
import time

import i2cdriver

import ssd1306

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
REG_ANIMATION = 0x0E
REG_ANIMATION_SETTINGS = 0x0F
REG_LED_BASE = 0x10
REG_LED_COUNT = 12
REG_GP_BASE = 0x20

CFG_ROT_RESET_ON_READ = 1 << 0
CFG_PUSH_RESET_ON_READ = 1 << 1
CFG_ENC_DIR_FLIP = 1 << 2
CFG_PUSH_COUNT_DEC = 1 << 3
CFG_PUSH_STATE_FLIP = 1 << 4
CFG_LED_LINEAR = 1 << 5

# Expected firmware version, read from the firmware Makefile if available
# (set through FW_VERSION_MAJOR/FW_VERSION_MINOR, exposed big-endian in
# registers 0x00-0x01). If the Makefile cannot be parsed the version step
# falls back to reporting what the device reports.
def read_fw_version():
    makefile = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "firmware", "src", "Makefile")
    values = {}
    try:
        with open(makefile) as f:
            for line in f:
                if "?=" not in line:
                    continue
                key, value = line.split("?=", 1)
                key = key.strip()
                if key in ("FW_VERSION_MAJOR", "FW_VERSION_MINOR"):
                    values[key] = value.strip()
        return [int(values["FW_VERSION_MAJOR"]), int(values["FW_VERSION_MINOR"])]
    except (OSError, KeyError, ValueError):
        return None

FW_VERSION = read_fw_version()

ANIMATIONS = [
    (0x00, "IDLE", "LEDs are driven manually from registers 0x10-0x1B"),
    (0x01, "LOADING", "rotating loading pattern"),
    (0x02, "FLASHING", "all LEDs flash on and off"),
    (0x03, "PULSING", "all LEDs pulse in brightness"),
    (0x04, "BREATHING", "all LEDs fade in and out"),
    (0x80, "FOLLOWING", "every 4th LED lit, pattern follows rotation"),
    (0x81, "POINT", "single LED points at the rotation position"),
    (0x82, "GAUGE", "gauge fill level follows rotation, 0-100 by default; the maximum is settable through 0x0F (count resets on entry)"),
    (0xFE, "ALL_ON", "all LEDs on at the brightness set through 0x0F"),
    (0xFF, "ALL_OFF", "all LEDs off"),
]

# Settings register (0x0F) scale (ms per step, or None for a brightness
# setting), default value and description for the animations that use it.
ANIMATION_SETTINGS = {
    0x01: (10, 10, "LOADING step time is value * 10 ms (default 10 = 100 ms)"),
    0x02: (10, 25, "FLASHING toggle time is value * 10 ms (default 25 = 250 ms)"),
    0x03: (1, 10, "PULSING brightness delay is value * 1 ms (default 10 = 10 ms)"),
    0xFE: (None, 0x3F, "ALL_ON brightness is the value (default 0x3F = full on)"),
    0x80: (None, 0, "FOLLOWING non-lit LED brightness is the value (default 0 = off)"),
    0x81: (None, 0, "POINT non-lit LED brightness is the value (default 0 = off)"),
    0x82: (None, 100, "GAUGE maximum is the value (default 100 = 0-100 range)"),
}

# Full-on LED register value (CHARLIE_PWM_STEPS - 1 in the firmware)
LED_FULL = 63

def read_gauge_config():
    """Parse the gauge layout and the unused-LED dimming build flags from the
    firmware's User/animations.h, so the expectations below track the firmware
    the board is running. Returns None if the header cannot be parsed."""
    header = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "firmware", "src", "User", "animations.h")
    try:
        with open(header) as f:
            content = f.read()
    except OSError:
        return None

    def define_int(name, default):
        for line in content.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[:2] == ["#define", name]:
                try:
                    return int(parts[2])
                except ValueError:
                    return default
        return default

    dim_defined = any(
        line.split()[:2] == ["#define", "ANIMATION_GAUGE_DIM_UNUSED_LEDS"]
        for line in content.splitlines()
    )
    return {
        "start_led": define_int("ANIMATION_GAUGE_START_LED", 8),
        "led_count": define_int("ANIMATION_GAUGE_LED_COUNT", 9),
        "default_max": define_int("ANIMATION_GAUGE_SETTINGS_DEFAULT", 100),
        # None when the dim flag is commented out: the unused LEDs keep their
        # previous state and are not checked
        "unused_brightness": define_int("ANIMATION_GAUGE_UNUSED_BRIGHTNESS", 8) if dim_defined else None,
    }

GAUGE_CONFIG = read_gauge_config()

if GAUGE_CONFIG is not None and 0x82 in ANIMATION_SETTINGS:
    # keep the expected GAUGE default in step_animations in sync with the
    # firmware's ANIMATION_GAUGE_SETTINGS_DEFAULT
    scale, _, description = ANIMATION_SETTINGS[0x82]
    ANIMATION_SETTINGS[0x82] = (scale, GAUGE_CONFIG["default_max"], description)

i2c = i2cdriver.I2CDriver(sys.argv[1] if len(sys.argv) > 1 else PORT)
oled = ssd1306.probe(i2c)
if oled is None:
    print("No SSD1306 OLED found (optional board component); running console-only")

failures = []
checks_run = 0

OLED_TITLE_ROWS = 16
OLED_LINE_HEIGHT = 8
OLED_STATUS_LINES = (ssd1306.HEIGHT - OLED_TITLE_ROWS) // OLED_LINE_HEIGHT
oled_status_line = 0


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


def oled_clear_status():
    if oled is None:
        return
    oled.fill_rect(0, OLED_TITLE_ROWS, oled.width, oled.height - OLED_TITLE_ROWS, False)


def oled_begin_step(step_no, total, title):
    """Draw the title bar: the top 16 rows, inverted, with the step counter."""
    global oled_status_line
    if oled is None:
        return
    oled.clear()
    oled.fill_rect(0, 0, oled.width, OLED_TITLE_ROWS, True)
    counter = f"{step_no}/{total}"
    counter_w = oled.text_width(counter)
    title_chars = max(0, (oled.width - counter_w - 10) // 6)
    title = title[:title_chars]
    oled.text(2, 4, title, on=False)
    oled.text(oled.width - counter_w - 2, 4, counter, on=False)
    oled_status_line = 0
    oled.flush()


def oled_msg(text):
    global oled_status_line
    if oled is None:
        return
    if oled_status_line < OLED_STATUS_LINES:
        oled.text(2, OLED_TITLE_ROWS + oled_status_line * OLED_LINE_HEIGHT, text[:21])
        oled.flush()
    oled_status_line += 1


def oled_live(count, state, push_count, hint="PRESS TO CONT"):
    if oled is None:
        return
    oled_clear_status()
    oled.text(2, OLED_TITLE_ROWS + 0 * OLED_LINE_HEIGHT, f"ROT  {count:+6d}")
    oled.text(2, OLED_TITLE_ROWS + 1 * OLED_LINE_HEIGHT, f"BTN  {state}")
    oled.text(2, OLED_TITLE_ROWS + 2 * OLED_LINE_HEIGHT, f"CNT  {push_count:3d}")
    oled.text(2, OLED_TITLE_ROWS + 5 * OLED_LINE_HEIGHT, hint)
    oled.flush()


def check(condition, ok_msg, fail_msg):
    global checks_run
    checks_run += 1
    if condition:
        print(f"  PASS: {ok_msg}")
        oled_msg(f"PASS {ok_msg}")
    else:
        print(f"  FAIL: {fail_msg}")
        oled_msg(f"FAIL {fail_msg}")
        failures.append(fail_msg)
    return condition


def wait_press_release(pressed_value=1, poll_s=0.02):
    while get_push_state() != pressed_value:
        time.sleep(poll_s)
    while get_push_state() == pressed_value:
        time.sleep(poll_s)


def live_encoder_display(note="", pressed_value=1, hint="PRESS TO CONT", poll_s=0.02):
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
        oled_live(count, state, push_count, hint)
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
    oled_msg(f"FW  {version[0]}.{version[1]:02d}")
    if FW_VERSION is None:
        print(f"  Firmware Makefile not found; device reports version {version[0]}.{version[1]:02d}")
        return
    check(version == FW_VERSION, f"firmware version is {version}", f"unexpected firmware version {version} (expected {FW_VERSION})")


def step_gp_ram():
    pattern = [0xA5, 0x5A, 0x00, 0xFF]
    set_regs(REG_GP_BASE, pattern)
    readback = read_regs(REG_GP_BASE, len(pattern))
    print(f"  GP RAM 0x20 write {pattern}, read back {readback}")
    oled_msg(f"RAM {' '.join(f'{v:02X}' for v in readback)}")
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
    oled_msg("IDLE animation set")
    print("  LED chase: each of the 12 LEDs lights up in turn")
    oled_msg("LED chase...")
    for led in range(REG_LED_COUNT):
        set_regs(REG_LED_BASE, [63 if i == led else 0 for i in range(REG_LED_COUNT)])
        time.sleep(0.12)
    set_regs(REG_LED_BASE, [0] * REG_LED_COUNT)

    print("  Brightness ramp on all LEDs (0 to 63 and back)")
    oled_msg("LED ramp...")
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
    oled_msg("GAMMA 32")
    wait_press_release()

    set_reg(REG_CONFIG, CFG_LED_LINEAR)
    print("  Same value 32 with CFG_LED_LINEAR (visually brighter, linear PWM)")
    oled_msg("LINEAR 32")
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
    live_encoder_display(hint="RESET ON READ")


    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")
    oled_msg("config restored")


def step_push_count():
    print("  Default mode: push count increments on every press (wraps past 255)")
    print("  Press the encoder button a few times and watch the count go up")
    oled_msg("press to count up")
    live_encoder_display()
    count = get_push_count()
    check(count != 0, f"push count registered presses (now {count})", "push count never incremented")

    set_reg(REG_CONFIG, CFG_PUSH_RESET_ON_READ)
    print("  CFG_PUSH_RESET_ON_READ set: the count clears to 0 after every read")
    live_encoder_display(hint="RESET ON READ")
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")
    oled_msg("config restored")


def step_push_count_dec():
    set_reg(REG_ENC_PUSH_COUNT, 5)
    set_reg(REG_CONFIG, CFG_PUSH_COUNT_DEC)
    print("  Push count preset to 5, CFG_PUSH_COUNT_DEC set: presses now count DOWN")
    print("  Press the encoder button and watch it decrement (saturates at 0)")
    oled_msg("press to count down")
    live_encoder_display()
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")
    oled_msg("config restored")


def step_push_state_flip():
    set_reg(REG_CONFIG, CFG_PUSH_STATE_FLIP)
    print("  CFG_PUSH_STATE_FLIP set: state shows 1 while released and 0 while pressed")
    print("  The step ends on a press, so expect the display to read 0 right before it ends")
    live_encoder_display(pressed_value=0, hint="FLIPPED: RELEASE")
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")
    oled_msg("config restored")


def step_enc_dir_flip():
    set_reg(REG_CONFIG, CFG_ENC_DIR_FLIP)
    print("  CFG_ENC_DIR_FLIP set: rotation direction is inverted")
    print("  Rotate both ways: clockwise should now decrement the count")
    live_encoder_display(hint="DIRECTION FLIP")
    set_reg(REG_CONFIG, 0x00)
    print("  Config restored to 0x00")
    oled_msg("config restored")


def step_animations():
    set_reg(REG_ENC_COUNT_HI, 0x00)
    set_reg(REG_ENC_COUNT_LO, 0x00)
    set_reg(REG_ANIMATION, 0x7F)
    readback = read_regs(REG_ANIMATION, 1)
    check(readback == [0x7F], "unknown animation id stored in register 0x0E", f"animation register readback {readback}")
    print("  Unknown animation falls back to IDLE (LEDs still follow registers 0x10-0x1B)")

    for anim_id, name, description in ANIMATIONS:
        print(f"\n  --- Animation 0x{anim_id:02X} {name}: {description}")
        set_reg(REG_ANIMATION, anim_id)
        readback = read_regs(REG_ANIMATION, 1)
        check(readback == [anim_id], f"animation {name} selected", f"animation register readback {readback} for {name}")
        oled_msg(f"0x{anim_id:02X} {name}")
        if anim_id in ANIMATION_SETTINGS:
            scale, default, settings_description = ANIMATION_SETTINGS[anim_id]
            settings = read_regs(REG_ANIMATION_SETTINGS, 1)
            check(settings == [default], f"{name} settings default 0x{default:02X} loaded into 0x0F", f"animation settings readback {settings} for {name} (expected {[default]})")
            print(f"  {settings_description}")
            live_encoder_display()
            if anim_id == 0xFE:
                new_value = 0x20
                set_reg(REG_ANIMATION_SETTINGS, new_value)
                settings = read_regs(REG_ANIMATION_SETTINGS, 1)
                check(settings == [new_value], f"{name} brightness setting writable (0x{new_value:02X})", f"animation settings readback {settings} for {name} (expected {[new_value]})")
                oled_msg(f"ALL ON 0x{new_value:02X}")
                print(f"  All-on brightness changed to 0x{new_value:02X}; press to continue")
            elif anim_id == 0x82:
                # the full 0x0F maximum check runs while GAUGE is active
                gauge_range_checks()
            elif anim_id in (0x80, 0x81):
                new_value = 8
                set_reg(REG_ANIMATION_SETTINGS, new_value)
                settings = read_regs(REG_ANIMATION_SETTINGS, 1)
                check(settings == [new_value], f"{name} off-brightness setting writable (0x{new_value:02X})", f"animation settings readback {settings} for {name} (expected {[new_value]})")
                oled_msg(f"OFF BRT 0x{new_value:02X}")
                print(f"  Non-lit LED brightness changed to 0x{new_value:02X}; press to continue")
            else:
                new_value = 5 if default != 5 else 6
                set_reg(REG_ANIMATION_SETTINGS, new_value)
                settings = read_regs(REG_ANIMATION_SETTINGS, 1)
                check(settings == [new_value], f"{name} timing setting writable (0x{new_value:02X} = {new_value * scale} ms)", f"animation settings readback {settings} for {name} (expected {[new_value]})")
                oled_msg(f"SET 0x{new_value:02X} = {new_value * scale} ms")
                print(f"  Timing setting changed to 0x{new_value:02X} ({new_value * scale} ms); press to continue")
        live_encoder_display()


def gauge_arc_led(i, start_led):
    return (i + start_led) % REG_LED_COUNT


def gauge_config():
    """Gauge layout and unused-LED brightness to expect, either parsed from the
    firmware's User/animations.h (so the expectations track the firmware the
    board is running) or the documented defaults if the header is missing."""
    if GAUGE_CONFIG is None:
        return {"start_led": 8, "led_count": 9, "default_max": 100, "unused_brightness": 8}
    return GAUGE_CONFIG


def gauge_expected_levels(count, maximum, start_led, led_count, unused_brightness):
    """Mirror of the firmware's gauge math: each arc LED covers an equal
    slice of the range; fully lit above its slice, off below it, fractional
    brightness (truncated) inside it."""
    levels = {}
    for i in range(led_count):
        slice_start = i * maximum / led_count
        slice_end = (i + 1) * maximum / led_count
        if count >= slice_end:
            level = LED_FULL
        elif count <= slice_start:
            level = 0
        else:
            level = int((count - slice_start) / (slice_end - slice_start) * LED_FULL)
        levels[gauge_arc_led(i, start_led)] = level
    for i in range(REG_LED_COUNT):
        if i not in levels and unused_brightness is not None:
            levels[i] = unused_brightness
    return levels


def gauge_apply_and_check(count, maximum, label, start_led, led_count, unused_brightness):
    """Write an encoder count directly and check the gauge's clamping and LED
    bar against it: the count registers are host-writable, so the range is
    verified end to end without waiting on the user."""
    value = count & 0xFFFF
    set_reg(REG_ENC_COUNT_HI, value >> 8)
    set_reg(REG_ENC_COUNT_LO, value & 0xFF)
    time.sleep(0.15)  # the gauge updates every 50 ms
    expected = gauge_expected_levels(count, maximum, start_led, led_count, unused_brightness)
    leds = read_regs(REG_LED_BASE, REG_LED_COUNT)
    clamped = max(0, min(count, maximum))
    read_count = get_encoder_count()
    check(read_count == clamped, f"{label}: count clamped to {clamped}", f"{label}: count readback {read_count}, expected clamp {clamped}")
    # only the arc LEDs (and the dimmed unused ones) are owned by the
    # animation; when the dim flag is off the unused LEDs are unchecked
    mismatched = [(i, leds[i], expected[i]) for i in expected if leds[i] != expected[i]]
    check(not mismatched, f"{label}: LED bar matches the expected levels", f"{label}: LED mismatch (led, actual, expected): {mismatched}")


def gauge_range_checks():
    """Verify the gauge maximum (0x0F) against the LED bar and the count
    clamping, for the default range first, then 0-10 and 0-255."""
    config = gauge_config()
    start_led = config["start_led"]
    led_count = config["led_count"]
    default_max = config["default_max"]
    unused_brightness = config["unused_brightness"]

    print(f"  Default 0-{default_max} range: count above, below and at the middle of the range")
    gauge_apply_and_check(default_max + 50, default_max, f"above max ({default_max + 50})", start_led, led_count, unused_brightness)
    gauge_apply_and_check(-5, default_max, "negative", start_led, led_count, unused_brightness)
    gauge_apply_and_check(default_max // 2, default_max, f"half ({default_max // 2})", start_led, led_count, unused_brightness)

    print("  Writing 0 to 0x0F keeps the current maximum")
    set_reg(REG_ANIMATION_SETTINGS, 0x00)
    settings = read_regs(REG_ANIMATION_SETTINGS, 1)
    check(settings == [default_max], "zero write to 0x0F is ignored", f"GAUGE settings changed to {settings} on a zero write")

    print("  Range changed to 0-10 through 0x0F")
    set_reg(REG_ANIMATION_SETTINGS, 10)
    settings = read_regs(REG_ANIMATION_SETTINGS, 1)
    check(settings == [10], "GAUGE maximum set to 10", f"GAUGE settings readback {settings} (expected [10])")
    gauge_apply_and_check(15, 10, "above max (15, range 0-10)", start_led, led_count, unused_brightness)
    gauge_apply_and_check(-3, 10, "negative (range 0-10)", start_led, led_count, unused_brightness)
    gauge_apply_and_check(5, 10, "half (5, range 0-10)", start_led, led_count, unused_brightness)

    print("  Range changed to 0-255 through 0x0F")
    set_reg(REG_ANIMATION_SETTINGS, 255)
    settings = read_regs(REG_ANIMATION_SETTINGS, 1)
    check(settings == [255], "GAUGE maximum set to 255", f"GAUGE settings readback {settings} (expected [255])")
    gauge_apply_and_check(128, 255, "half (128, range 0-255)", start_led, led_count, unused_brightness)
    gauge_apply_and_check(300, 255, "above max (300, range 0-255)", start_led, led_count, unused_brightness)

    print("  Live check: rotate the encoder and watch the 0-255 gauge fill; press to continue")
    live_encoder_display(hint="GAUGE 0-255")

    set_reg(REG_ANIMATION_SETTINGS, default_max)


def step_soft_reset():
    version_before = read_regs(REG_FW_VERSION_HI, 2)
    set_regs(REG_GP_BASE, [0xA5])
    print("  Writing 0x01 to register 0x00 issues a soft reset")
    oled_msg("resetting MCU...")
    set_reg(REG_SOFT_RESET, 0x01)
    time.sleep(0.5)
    version = read_regs(REG_FW_VERSION_HI, 2)
    check(version == version_before, f"device back after reset (version {version})", f"no response after soft reset (version {version}, before reset {version_before})")
    gp = read_regs(REG_GP_BASE, 1)
    check(gp == [0x00], "GP RAM back to power-on default 0x00", f"GP RAM after reset {gp}")
    anim = read_regs(REG_ANIMATION, 1)
    check(anim == [0x80], "animation back to default FOLLOWING", f"animation after reset {anim}")
    settings = read_regs(REG_ANIMATION_SETTINGS, 1)
    check(settings == [0x00], "animation settings back to default 0x00 for FOLLOWING", f"animation settings after reset {settings}")
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


def oled_results_screen(total_checks, passed_checks):
    if oled is None:
        return
    oled.clear()
    oled.fill_rect(0, 0, oled.width, OLED_TITLE_ROWS, True)
    oled.text(2, 4, "RESULTS", on=False)
    oled.text(2, OLED_TITLE_ROWS + 0 * OLED_LINE_HEIGHT, f"CHECKS {passed_checks}/{total_checks}")
    if failures:
        oled.text(2, OLED_TITLE_ROWS + 1 * OLED_LINE_HEIGHT, f"FAILED {len(failures)}")
        for i, failure in enumerate(failures[: OLED_STATUS_LINES - 2]):
            oled.text(2, OLED_TITLE_ROWS + (2 + i) * OLED_LINE_HEIGHT, f"- {failure}"[:21])
    else:
        oled.text(2, OLED_TITLE_ROWS + 1 * OLED_LINE_HEIGHT, "ALL CHECKS PASSED")
        oled.text_centered(OLED_TITLE_ROWS + 4 * OLED_LINE_HEIGHT, "TEST COMPLETE")
    oled.flush()


def main():
    print("rack-ui interactive test suite")
    print(f"I2C slave address: 0x{SLAVE_ADDR:02X}")
    oled_begin_step(0, len(STEPS), "RACK-UI TEST")
    oled_msg(f"MCU @ 0x{SLAVE_ADDR:02X}")
    if oled is not None:
        oled_msg(f"OLED @ 0x{oled.address:02X}")
    oled_msg("press encoder to")
    oled_msg("advance each step")
    try:
        for index, (title, func) in enumerate(STEPS, 1):
            print("\n" + "=" * 70)
            print(f" Step {index}/{len(STEPS)}: {title}")
            print("=" * 70)
            before = len(failures)
            oled_begin_step(index, len(STEPS), title)
            func()
            step_failures = len(failures) - before
            if oled is not None:
                oled.text(
                    oled.width - 30,
                    OLED_TITLE_ROWS + (OLED_STATUS_LINES - 1) * OLED_LINE_HEIGHT,
                    "STEP FAIL" if step_failures else "STEP OK",
                )
                oled.flush()
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
    oled_results_screen(checks_run, checks_run - len(failures))


if __name__ == "__main__":
    main()
