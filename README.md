# Rack UI

![Rack UI - Test results](docs/rack_ui_test_pass.jpeg)
![Rack UI - connected](docs/rack_ui-connected.jpeg)

## Contents

- [**Firmware**](firmware/src/README.md) - firmware for the Rack UI front panel controller board:
  a Puya PY32F002Ax5 (Arm Cortex-M0+) acting as an I2C peripheral, exposing a rotary encoder,
  a push button and a charlieplexed LED array to a host over a single I2C bus. Includes the
  register map, build and flashing instructions, and prebuilt release binaries.

## Pinout

![J5 expansion connector schematic](./docs/io-header.png)

| Pin | Signal |
|---|---|
| 1 | NC |
| 2 | +5V in |
| 3 | RX_EN LED (green) |
| 4 | TX_EN LED (amber) |
| 5 | Target TXD / HT42B534-2 IC RXD |
| 6 | Target RXD / HT42B534-2 IC TXD |
| 7 | SCL |
| 8 | SDA |
| 9 | NC |
| 10 | NC |
| 11 | GND |
| 12 | GND |

---

## License & Acknowledgements

- PY32 Template from [IOsetting](https://github.com/IOsetting/py32f0-template)

Made with ❤️, lots of ☕️, and lack of 🛌  
Published under CreativeCommons BY-SA 4.0

[![Creative Commons License](https://i.creativecommons.org/l/by-sa/4.0/88x31.png)](http://creativecommons.org/licenses/by-sa/4.0/)  
This work is licensed under a [Creative Commons Attribution-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-sa/4.0/).
