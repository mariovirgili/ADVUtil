# Switch Wireless Architecture

## Goal

Add a future `switch-wireless` implementation without changing the behavior of AirMouse or GPS and without mixing Switch-specific transport code into the current Arduino BLE gamepad path.

The right target is not "add another descriptor to `src/Gamepad.cpp`". The right target is:

- keep `Gamepad` as the menu entry and Cardputer UX entry point;
- split the current monolithic gamepad mode into reusable layers;
- keep the current Windows BLE backend as one transport;
- add a separate Switch wireless transport/backend later.

## Current constraints

- The current app is `PlatformIO + Arduino` in [Platformio.ini](/d:/CardputerADV/Compile/ADVUtil/Platformio.ini).
- The current gamepad mode is monolithic in [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp).
- `Gamepad.cpp` currently mixes:
  - persistent settings
  - Cardputer keyboard/IMU input mapping
  - BLE transport
  - UI rendering
  - menu/help/settings state machine
- AirMouse and GPS are independent modes and should stay untouched:
  - [src/AirMouse.cpp](/d:/CardputerADV/Compile/ADVUtil/src/AirMouse.cpp)
  - [src/GPSInfo.cpp](/d:/CardputerADV/Compile/ADVUtil/src/GPSInfo.cpp)
- `main.cpp` should stay almost unchanged and continue calling the public `gamepadInit()`, `gamepadResetUI()`, and `gamepadLoop()` API from [src/Gamepad.h](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.h).

## Proposed architecture

Use 3 layers.

1. Cardputer Input Layer

- Reads keyboard, BtnA, and IMU.
- Produces a host-agnostic snapshot and report intent.
- Contains no BLE code.

2. Gamepad Core Layer

- Owns settings, profile selection, motion calibration state, menu state, and report model.
- Knows nothing about Windows BLE vs Switch wireless.
- Exposes a simple transport interface: `begin`, `isConnected`, `sendReport`, `shutdown`.

3. Transport Layer

- One backend for the current Windows BLE implementation.
- One future backend for Switch wireless.
- The Switch backend should live in a separate target/subproject, not inside the current generic BLE library path.

## Recommended repo layout

```text
docs/
  switch-wireless-architecture.md
lib/
  gamepad_core/
    include/
      GamepadTypes.h
      GamepadSettings.h
      GamepadSession.h
      GamepadReport.h
      CardputerInputSnapshot.h
      IGamepadTransport.h
    src/
      GamepadSettings.cpp
      GamepadSession.cpp
      GamepadReport.cpp
src/
  Gamepad.cpp
  Gamepad.h
  gamepad/
    CardputerGamepadInput.cpp
    CardputerGamepadInput.h
    GamepadStorageArduino.cpp
    GamepadStorageArduino.h
    GamepadUi.cpp
    GamepadUi.h
    WindowsBleGamepadTransport.cpp
    WindowsBleGamepadTransport.h
switch-wireless/
  README.md
  platformio.ini or CMakeLists.txt
  main/
  components/
```

## Exact split from the current `src/Gamepad.cpp`

These are the pieces to separate first.

### 1. Shared core types

Create:

- `lib/gamepad_core/include/GamepadTypes.h`
- `lib/gamepad_core/include/GamepadReport.h`

Move from [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp):

- enums and structs around lines `17-76`
- `GPReportState` around lines `68-76`
- label/parser helpers around lines `174-321`
- report comparison and neutral-report helpers around lines `610-648`

Responsibility:

- own `GPProfile`, `GPMoveInput`, `GPBindingKey`, `GPReportState`
- own binding labels/parsers and profile labels/parsers
- own report equality and neutral-state creation

### 2. Shared settings/state core

Create:

- `lib/gamepad_core/include/GamepadSettings.h`
- `lib/gamepad_core/src/GamepadSettings.cpp`
- `lib/gamepad_core/include/GamepadSession.h`
- `lib/gamepad_core/src/GamepadSession.cpp`

Move from [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp):

- defaults around lines `322-337`
- config line parsing around lines `350-391`
- menu state update logic around lines `828-960`
- exit-arm helpers around lines `431-435` and `898-923`

Responsibility:

- own in-memory settings structure
- own menu/help/runtime state
- own menu mutation rules and exit gesture state
- contain no SD, display, `M5Cardputer`, or BLE dependencies

### 3. Arduino storage adapter

Create:

- `src/gamepad/GamepadStorageArduino.h`
- `src/gamepad/GamepadStorageArduino.cpp`

Move from [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp):

- `writeGPConfigLine` around lines `338-348`
- `loadGPSettings` around lines `393-405`
- `saveGPSettings` around lines `407-429`

Responsibility:

- translate shared `GamepadSettings` to and from `/ADVUtil/gamepad.cfg`
- keep `SD.h` and filesystem code out of the shared core

### 4. Cardputer input mapper

Create:

- `src/gamepad/CardputerGamepadInput.h`
- `src/gamepad/CardputerGamepadInput.cpp`

Move from [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp):

- keyboard helpers around lines `237-321`
- axis/hat helpers around lines `439-463`
- motion calibration and motion application around lines `472-534`
- report building around lines `654-697`

Responsibility:

- read `M5Cardputer.Keyboard`, `M5.BtnA`, and `M5.Imu`
- produce `GPReportState`
- keep all Cardputer-specific input logic in one place
- this is the main logic to reuse for Switch wireless

### 5. UI renderer

Create:

- `src/gamepad/GamepadUi.h`
- `src/gamepad/GamepadUi.cpp`

Move from [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp):

- canvas/color helpers around lines `122-171`
- status and summary text around lines `535-598`
- main/help/menu rendering around lines `699-824`

Responsibility:

- render from a read-only session/view model
- know about `M5Canvas`, colors, and layout
- know nothing about BLE implementation details

### 6. Windows BLE transport

Create:

- `src/gamepad/WindowsBleGamepadTransport.h`
- `src/gamepad/WindowsBleGamepadTransport.cpp`

Move from [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp):

- `BleGamepad bleGamepad(...)` near line `12`
- `applyGPReport` around lines `623-640`
- BLE startup around lines `972-989`

Responsibility:

- wrap `ESP32-BLE-Gamepad`
- implement `IGamepadTransport`
- keep the current Windows path working exactly as today

### 7. Thin mode facade

Keep:

- [src/Gamepad.h](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.h)
- [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp)

But reduce `src/Gamepad.cpp` to:

- `gamepadInit()`
- `gamepadResetUI()`
- `gamepadLoop()`
- transport selection for current build target
- wiring between session, input mapper, renderer, and transport

This file should become the composition root, not the implementation dump.

## Switch wireless target

The Switch backend should be isolated under `switch-wireless/`, not mixed into the current Arduino-only transport.

Reason:

- the current generic BLE stack is Windows-oriented;
- a real Switch wireless backend will likely need a different transport/runtime stack;
- flash headroom is already tight, so keeping the Switch work as a separate target is safer.

Recommended shape:

- `switch-wireless/` consumes the same shared `lib/gamepad_core/`
- it reuses the same `CardputerGamepadInput` model or a thin port of it
- it provides its own `SwitchWirelessTransport`

## Files that should stay untouched

Do not modify:

- [src/AirMouse.cpp](/d:/CardputerADV/Compile/ADVUtil/src/AirMouse.cpp)
- [src/AirMouse.h](/d:/CardputerADV/Compile/ADVUtil/src/AirMouse.h)
- [src/GPSInfo.cpp](/d:/CardputerADV/Compile/ADVUtil/src/GPSInfo.cpp)
- [src/GPSInfo.h](/d:/CardputerADV/Compile/ADVUtil/src/GPSInfo.h)

Only minimal changes should be allowed in:

- [src/main.cpp](/d:/CardputerADV/Compile/ADVUtil/src/main.cpp)
- [src/Gamepad.h](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.h)
- [src/Gamepad.cpp](/d:/CardputerADV/Compile/ADVUtil/src/Gamepad.cpp)

## Implementation order

1. Extract shared types and settings from `src/Gamepad.cpp`.
2. Extract `CardputerGamepadInput`.
3. Extract `GamepadUi`.
4. Extract `WindowsBleGamepadTransport`.
5. Reduce `src/Gamepad.cpp` to a thin facade and verify behavior matches current Windows mode.
6. Create `switch-wireless/` as a separate target and add the future Switch transport there.

## Recommendation

Do the refactor first and keep behavior identical on the current Windows BLE mode.

Only after that should `switch-wireless` start. If Switch work begins before this split, the repo will end up with two incompatible concerns fused inside one already-large `Gamepad.cpp`.
