# ADVUtil
AirMouse and GPS integrated

ADVUtil for Cardputer: Air Mouse BLE + GPS Info utility with maps, satellite views, trip stats and more
I’ve been working on ADVUtil, a utility app for the M5Stack Cardputer with two main tools built in:

- Air Mouse BLE Turn the Cardputer into a Bluetooth air mouse using the IMU.

Main features:

Tilt-based mouse control
Left click with Enter
Right click with Space
IMU calibration directly from the device
Adjustable sensitivity
Settings saved to SD card

- GPS Info

A full GPS utility designed for use with the official M5Stack LoRa Cap 1262, based on https://github.com/DevinWatson/Cardputer-Adv-GPS-Info
https://github.com/alcor55/Cardputer-GPS-Info

Main features:

Live GPS data and fix status
Sky view and signal bars
Satellite constellation overview
Coordinates, altitude, speed and course
Trip statistics
Breadcrumb track
Altitude and speed graphs
NMEA monitor
GPS clock
Map view with zoom
3D globe view
Configurable RX / TX / baud settings
Optional slideshow mode between screens
SD card config saving

Important note:
To use the GPS feature, you need an official M5Stack LoRa Cap 1262.

Air Mouse troubleshooting:
If Air Mouse does not work correctly on your PC, the usual fix is:
Disable Bluetooth on the PC
Remove the previously paired Cardputer mouse device
Re-enable Bluetooth
Pair the Cardputer again from scratch

In many cases, reconnecting from zero fixes detection and input issues.
If there is interest, I can also share more screenshots, controls, and build details.

______________________

V0.3 changelog:

New in this version:

Arrow keys can now be used for mouse scrolling
Added a universal back action with Del
Added X/Y axis inversion options for Air Mouse
Added configurable button mapping for left click, right click, middle click, back, and forward
Air Mouse settings are now saved persistently

Thanks to u/Russian_man_ who pointed these out, especially the suggestion about axis inversion and customizable mouse buttons. Those were genuinely useful improvements.

If you test the new version, let me know how the new Air Mouse controls feel and whether the default button mappings make sense or should be changed.
