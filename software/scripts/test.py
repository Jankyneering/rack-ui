import i2cdriver

# Initialize the I2CDriver
i2c = i2cdriver.I2CDriver("/dev/tty.usbserial-DM02V7KY") 

addr = 0x36

# 1. Test Slave Receive (Master Writes)
# Your code expects 15 bytes in the buffer
i2c.start(addr, 0)
i2c.write(bytes([1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]))
i2c.start(addr, 1)
data = i2c.read(15)
i2c.stop()

# 2. Test Slave Transmit (Master Reads)
# Your code expects to return 15 bytes from aTxBuffer
print(f"Slave replied: {data}")




# import i2cdriver
# import time

# i2c = i2cdriver.I2CDriver("/dev/tty.usbserial-DM02V7KY")
# SLAVE_ADDR = 0x36

# # ==========================================================
# # TEST 1: READ REGISTER 0x00 (Should output [170] which is 0xAA)
# # ==========================================================
# i2c.start(SLAVE_ADDR, 0) # Write mode to send pointer
# i2c.write(bytes([0x00]))  # Set pointer to register 0x00
# i2c.start(SLAVE_ADDR, 1) # Repeated start into read mode
# reg0 = i2c.read(1)
# i2c.stop()
# print(f"Register 0x00 Default Read: {list(reg0)} (Expected: [170])")

# # ==========================================================
# # TEST 2: READ REGISTER 0x01 (Should output [187] which is 0xBB)
# # ==========================================================
# i2c.start(SLAVE_ADDR, 0)
# i2c.write(bytes([0x01]))  # Set pointer to register 0x01
# i2c.start(SLAVE_ADDR, 1)
# reg1 = i2c.read(1)
# i2c.stop()
# print(f"Register 0x01 Default Read: {list(reg1)} (Expected: [187])")

# # ==========================================================
# # TEST 3: WRITE AND VERIFY OVERWRITE
# # ==========================================================
# print("\nWriting 0x55 to Register 0x00...")
# i2c.start(SLAVE_ADDR, 0)
# i2c.write(bytes([0x00, 0x55])) # Pointer 0x00, Data 0x55
# i2c.stop()

# time.sleep(0.01)

# # Read it back to verify
# i2c.start(SLAVE_ADDR, 0)
# i2c.write(bytes([0x00]))
# i2c.start(SLAVE_ADDR, 1)
# verify = i2c.read(1)
# i2c.stop()
# print(f"Register 0x00 After Write: {list(verify)} (Expected: [85])")
