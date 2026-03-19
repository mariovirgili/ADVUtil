# ADVUtil Changelog

All notable changes to this project should be documented here.

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
