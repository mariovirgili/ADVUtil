# Reddit Post - ADVUtil v0.5

## Suggested title

ADVUtil v0.5 for Cardputer released: Macro Mode + international keyboard layouts

## Post body

Hi all,

I just pushed ADVUtil v0.5 for the M5Stack Cardputer.

This update is mainly focused on the Air Mouse / Keyboard side of the project and adds a full macro workflow plus selectable keyboard layouts for non-US hosts.

Changelog:

- Added Macro Mode for both Air Mouse and Keyboard mode
- Hold BtnA for 2 seconds to enter or exit Macro Mode
- Added 10 macro slots on keys 1 to 0
- Press R to record or replace a macro
- Press ESC to stop and save the current recording
- Press L to open a scrollable list of saved macros
- Macro slots are saved on SD card and restored on startup
- Added a first-time keyboard layout setup screen
- Added a keyboard layout selector in the Air Mouse settings menu
- Live keyboard mode and macro playback now use the selected host keyboard layout
- Added international layouts based on the official Arduino Keyboard layouts: US, IT, FR, DE, DA, ES, HU, PT-BR, PT-PT and SV
- Added UK keyboard layout support as an extra selectable layout
- Cleaned up the macro UI and first-time setup screens
- Renamed the main menu entry to Air Mouse/Kbd

Important note:

- If you recorded macros on older builds, it is best to re-record them after updating so they match the new layout-aware system.

GPS Info is still included in ADVUtil as before, so this is not just an Air Mouse release, but v0.5 mainly improves the HID side of the project.

GitHub:
https://github.com/mariovirgili/ADVUtil

If anyone tests it on layouts other than US or IT, feedback is useful, especially for FR, DE, ES, PT and the other international layouts.
