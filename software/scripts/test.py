import i2cdriver

# Initialize the I2CDriver
i2c = i2cdriver.I2CDriver("/dev/tty.usbserial-DM02V7KY")
SLAVE_ADDR = 0x36


def set_reg(reg, value):
    i2c.start(SLAVE_ADDR, 0)
    i2c.write(bytes([reg, value]))
    i2c.stop()


def read_regs(reg, count):
    i2c.start(SLAVE_ADDR, 0)
    i2c.write(bytes([reg]))
    i2c.start(SLAVE_ADDR, 1)
    data = i2c.read(count)
    i2c.stop()
    return list(data)


# 1. Firmware version (registers 0x00-0x01, expects [0, 1])
print(f"FW version: {read_regs(0x00, 2)} (expected [0, 1])")

# 2. LED brightness: set LED 0 to half brightness (0x20)
set_reg(0x10, 0x20)
print(f"LED 0 brightness readback: {read_regs(0x10, 1)} (expected [32])")

# 3. General-purpose RAM scratch test (defaults to 0x00)
set_reg(0x20, 0xA5)
print(f"GP RAM readback: {read_regs(0x20, 1)} (expected [165])")

# 5. Encoder rotation count (clears on read only with config bit 0 set)
set_reg(0x03, 0x03)  # enable reset-on-read for rotation and push counts
print(f"Rotation count: {read_regs(0x04, 2)} (clears to [0, 0] on next read)")
print(f"Rotation count after clear: {read_regs(0x04, 2)} (expected [0, 0])")
set_reg(0x03, 0x00)  # restore defaults

# 6. Push button state and count
print(f"Push state: {read_regs(0x07, 1)} (0x01 while pressed)")
print(f"Push count: {read_regs(0x06, 1)} (clears to [0] on next read with bit 1 set)")

# 7. Soft reset: writing a non-zero value to 0x00 resets the MCU.
#    Registers return to power-on defaults (GP RAM back to 0x00).
#    NOTE: this restarts the device; run it last.
set_reg(0x00, 0x01)
print("Soft reset issued; re-run the FW version check to confirm")
print(f"FW version after reset: {read_regs(0x00, 2)} (expected [0, 1])")
print(f"GP RAM after reset: {read_regs(0x20, 1)} (expected [0])")
