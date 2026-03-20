# Reddit Post - ADVUtil v0.6

## Suggested title

ADVUtil v0.6 for Cardputer released: BLE Gamepad mode + motion controls

## Post body

Hi all,

I just pushed ADVUtil v0.6 for the M5Stack Cardputer.

This release adds a new BLE Gamepad mode alongside the existing Air Mouse / Keyboard and GPS tools.

Changelog:

- Added a new BLE Gamepad mode in the main menu
- Added a dedicated Gamepad UI with status, help and settings screens
- Added Twin Stick and Southpaw profiles
- Added Cardputer-friendly default controls: ASDE for movement and JKLO for aim
- Added configurable bindings for A, B, X, Y, L1, R1, Back and Start
- Added optional motion movement using the IMU for the left stick
- Added motion calibration, sensitivity control and X/Y axis inversion
- Added persistent Gamepad settings saved in `/ADVUtil/gamepad.cfg`
- Updated the main menu version to v0.6
- Reduced Gamepad UI flicker by moving rendering to an off-screen canvas

Current defaults:

- Left stick: ASDE
- Right stick: JKLO
- D-pad: ; , . /
- BtnA on the Cardputer = gamepad A
- Default extra bindings: B = Space, X = Z, Y = X, L1 = W, R1 = R, Back = 2, Start = 4

Important notes:

- Right now Air Mouse/Kbd and Gamepad should be treated as separate boot-time HID modes. If one is already initialized, reboot before switching to the other.
- Motion mode replaces the left-stick movement keys and can be tuned from the Gamepad settings menu.

GitHub:
https://github.com/mariovirgili/ADVUtil

If anyone tests the new Gamepad mode on Windows, feedback is useful, especially on motion sensitivity, axis inversion and whether the default Cardputer key layout feels right.
