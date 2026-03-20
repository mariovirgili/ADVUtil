# ADVUtil Changelog

All notable changes to this project should be documented here.

## v0.6.0

### Added

- Experimental `BLE Gamepad` mode for Cardputer, selectable from the main menu.
- Windows-friendly gamepad descriptor with `X/Y/Z/RZ`, one hat switch and 8 buttons.
- Dedicated Gamepad UI with main screen, help screen and settings screen.
- Configurable Gamepad profiles: `Twin Stick` and `Southpaw`.
- Cardputer-oriented default controls: `ASDE` for movement and `JKLO` for aim.
- Configurable button bindings for `A`, `B`, `X`, `Y`, `L1`, `R1`, `Back` and `Start`.
- `Move Input` selector to choose between keyboard movement and IMU motion movement.
- Motion calibration flow and adjustable motion sensitivity.
- Motion axis inversion options for both `X` and `Y`.
- Persistent Gamepad settings saved to `/ADVUtil/gamepad.cfg`.

### Changed

- Main menu version updated to `v0.6`.
- Main menu now includes a `Gamepad` entry alongside `Air Mouse/Kbd` and `GPS Info`.
- Gamepad UI rendering now uses an off-screen canvas to reduce visible flicker during redraws.
- Motion control was remapped so tilting up/down drives up/down and tilting left/right drives left/right.
- The vertical motion axis now starts inverted by default for more natural in-hand control.
- Exiting the Gamepad settings screen with backtick saves changes, while the UI labels it as `ESC`.

### Notes

- `Gamepad` and `Air Mouse/Kbd` currently should not be hot-swapped in the same boot session; if you already initialized one HID stack, reboot before switching to the other.
- This release is focused on the new Gamepad mode while keeping all `v0.5` Air Mouse, Keyboard and Macro features available.

## v0.5.0

### Added

- Macro Mode for both Air Mouse and Keyboard mode.
- Long press on `BtnA` for 2 seconds to enter or leave Macro Mode.
- Ten macro slots mapped to keys `1` through `0`.
- Macro recording flow with live on-screen preview.
- `ESC` to stop and save a macro recording.
- Scrollable macro list view with saved contents preview.
- SD card persistence for macro slots.
- First-time keyboard layout setup saved to `airmouse.cfg`.
- Keyboard layout selector inside the Air Mouse settings menu.
- International keyboard layouts from the official Arduino Keyboard layouts: `US`, `IT`, `FR`, `DE`, `DA`, `ES`, `HU`, `PT-BR`, `PT-PT`, `SV`.
- `UK` keyboard layout support as an extra selectable layout.

### Changed

- Keyboard mode now sends keys according to the selected host keyboard layout.
- Macro playback now follows the selected host keyboard layout as well.
- Macro UI screens were cleaned up so help text and status blocks fit correctly on screen.
- Backtick prompts in the macro UI were replaced with `ESC`.
- The main menu entry was renamed from `Air Mouse` to `Air Mouse/Kbd`.

### Notes

- Existing macros created on older builds may still load, but re-recording them is recommended for best compatibility with the new layout-aware macro system.
- This release is mainly focused on the Air Mouse and Keyboard side of ADVUtil. GPS features remain available as before.
