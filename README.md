# i2cu

i2cu is a simple i2c bus probe that reports addresses for listening devices on an i2c bus.

It can also monitor a serial UART

It works with 3.3v but may work with 5v devices as the ESP32 is 5v tolerant but not necessarily 5v capable.

Currently it's configured for a TTGO T1 Display

To use:
On the TTGO
wire ground to ground in your circuit
Wire SDA to 21
Wire SCL to 22

Or for serial wire 17 to a serial UART TX line.

The screen is designed to sleep after a brief period in case the TTGO is on battery. Press the left button to wake it up.

Holding the left button pauses any display.
Clicking the right button changes serial mode from text to binary
Long pressing the right button selects the baud rate.