# picoDarkroomTimer

picoDarkroomTimer is a digital darkroom measurement device and timer.
It is powered by a Raspberry Pi Pico microcontroller, a 2.8" SPI LCD (320x240 px), an encoder knob, 5 buttons and a beeper. Values can be stored in a serial EEPROM.

Darkroom light and Enlarger are controlled via an attached PCB with two relays and the power supply.

Light measurements are done with an Adafruit_TSL2591 sensor module, offering a very wide sensitivity range and true visible light measurements in Lux values.

The current software can work as a digital timer (0...99s) or a measurement device, that measures shadow point and light point, calculates the tonal range, recommends a (multigrade) paper grade and calculates resulting exposure time (after calibrating the paper sensitivity). The resulting time can be corrected in steps of 1/3 f.

You can program your own stuff on the device.