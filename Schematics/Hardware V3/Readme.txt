HW V3 holds all the required components in one PCB, making it smaller and easier to assemble. It has CAN and K-LINE interfaces.

Also, a small PCB with the buttons has been added.

The ZIP files contain the Gerber files to order your own ECU tool PCBs from OSHPark, which is one of the cheapest PCB manufacturers, or you can try  http://smart-prototyping.com/ too! (Thanks Jan Lüke!)

So just upload the files to any of those site and enjoy!

To check the connectors pinout, please check this file: https://github.com/fjvva/ecu-tool/blob/master/Schematics/Hardware%20V3/ECU%20tool%20V0.3.pdf

Components list is as follows:

ECU Tool HW V0.3D:

-IC1 - Mcp2515-I/SO
-1xMCP2551-I/SN
-1xArduino Mini Pro
-1x7805T
-1x16MHZ HC49S crystal
-2x22pF ceramic capacitors
-1xMC3320 (AKA 33290)
-1x1N4004 diode
-1xSD card reader (as in HWV2)
-1x 20x4 I2C LCD (As in HWV2)
-2x3v3 LED
Resistors are 1/8W, but 1/4W can be used too:
-5x10k ohm
-2x330 ohm
-1x510 ohm
-1x120 ohm
-1x1k ohm
-1x47uf 16v capacitor
-1x220uf 16v capacitor
IC3- 1x74HC32D

For Buttons PCB:

-4x 12x12mm momentary tactile switch (with cap)
