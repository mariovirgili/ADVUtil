#include "AirMouse.h"
#include <M5Cardputer.h>
#include <BleMouse.h>
#include <SD.h>

// Import the shared SD/menu state from main.cpp.
extern bool sdAvailable;
extern bool returnToMenu;

BleMouse bleMouse("Cardputer Mouse", "M5Stack", 100);

const char* amConfigPath = "/ADVUtil/airmouse.cfg";
bool amSettingsChanged = false;

// Variables to store the IMU calibration offsets.
float gyroXOffset = 0.0f;
float gyroYOffset = 0.0f;
float gyroZOffset = 0.0f;

// Fractional accumulators to ensure smooth movement even at very low sensitivity.
float fractionX = 0.0f;
float fractionY = 0.0f;
float sensitivity = 0.15f;
bool invertAxisX = false;
bool invertAxisY = false;

enum AMBindingKey : uint8_t {
    AM_BIND_NONE = 0,
    AM_BIND_ENTER,
    AM_BIND_SPACE,
    AM_BIND_BTNA,
    AM_BIND_Z,
    AM_BIND_X,
    AM_BIND_V,
    AM_BIND_B,
    AM_BIND_N,
    AM_BIND_COUNT
};

AMBindingKey leftClickBinding = AM_BIND_ENTER;
AMBindingKey rightClickBinding = AM_BIND_SPACE;
AMBindingKey middleClickBinding = AM_BIND_BTNA;
AMBindingKey backClickBinding = AM_BIND_NONE;
AMBindingKey forwardClickBinding = AM_BIND_NONE;

enum AMMenuItem : int {
    AM_MENU_SENSITIVITY = 0,
    AM_MENU_INVERT_X,
    AM_MENU_INVERT_Y,
    AM_MENU_LEFT_CLICK,
    AM_MENU_RIGHT_CLICK,
    AM_MENU_MIDDLE_CLICK,
    AM_MENU_BACK_CLICK,
    AM_MENU_FORWARD_CLICK,
    AM_MENU_EXIT,
    AM_MENU_COUNT
};

// State machine variables.
bool amInMenu = false;
int amMenuIndex = 0;
unsigned long amLastKeyPress = 0;
unsigned long amLastScrollTick = 0;
bool amWasConnected = false;

const char* getBindingLabel(AMBindingKey binding) {
    switch (binding) {
        case AM_BIND_NONE:  return "None";
        case AM_BIND_ENTER: return "Enter";
        case AM_BIND_SPACE: return "Space";
        case AM_BIND_BTNA:  return "BtnA";
        case AM_BIND_Z:     return "Z";
        case AM_BIND_X:     return "X";
        case AM_BIND_V:     return "V";
        case AM_BIND_B:     return "B";
        case AM_BIND_N:     return "N";
        default:            return "?";
    }
}

AMBindingKey parseBindingLabel(const String& value) {
    if (value.equalsIgnoreCase("none"))  return AM_BIND_NONE;
    if (value.equalsIgnoreCase("enter")) return AM_BIND_ENTER;
    if (value.equalsIgnoreCase("space")) return AM_BIND_SPACE;
    if (value.equalsIgnoreCase("btna"))  return AM_BIND_BTNA;
    if (value.equalsIgnoreCase("z"))     return AM_BIND_Z;
    if (value.equalsIgnoreCase("x"))     return AM_BIND_X;
    if (value.equalsIgnoreCase("v"))     return AM_BIND_V;
    if (value.equalsIgnoreCase("b"))     return AM_BIND_B;
    if (value.equalsIgnoreCase("n"))     return AM_BIND_N;
    return AM_BIND_NONE;
}

AMBindingKey cycleBinding(AMBindingKey current, int direction) {
    int next = (int)current + direction;
    if (next < 0) next = AM_BIND_COUNT - 1;
    if (next >= AM_BIND_COUNT) next = 0;
    return (AMBindingKey)next;
}

bool isAlphaKeyPressed(char lower, char upper) {
    return M5Cardputer.Keyboard.isKeyPressed(lower) || M5Cardputer.Keyboard.isKeyPressed(upper);
}

bool isBindingPressed(AMBindingKey binding, const Keyboard_Class::KeysState& keyState) {
    switch (binding) {
        case AM_BIND_NONE:  return false;
        case AM_BIND_ENTER: return keyState.enter;
        case AM_BIND_SPACE: return keyState.space;
        case AM_BIND_BTNA:  return M5.BtnA.isPressed();
        case AM_BIND_Z:     return isAlphaKeyPressed('z', 'Z');
        case AM_BIND_X:     return isAlphaKeyPressed('x', 'X');
        case AM_BIND_V:     return isAlphaKeyPressed('v', 'V');
        case AM_BIND_B:     return isAlphaKeyPressed('b', 'B');
        case AM_BIND_N:     return isAlphaKeyPressed('n', 'N');
        default:            return false;
    }
}

void releaseAllAMButtons() {
    if (!bleMouse.isConnected()) return;

    bleMouse.release(MOUSE_LEFT);
    bleMouse.release(MOUSE_RIGHT);
    bleMouse.release(MOUSE_MIDDLE);
    bleMouse.release(MOUSE_BACK);
    bleMouse.release(MOUSE_FORWARD);
}

void writeConfigLine(File& file, const char* key, const String& value) {
    file.print(key);
    file.print('=');
    file.println(value);
}

void applyConfigLine(const String& rawLine) {
    String line = rawLine;
    line.trim();
    if (!line.length()) return;

    const int separator = line.indexOf('=');
    if (separator < 0) {
        // Backward compatibility with the original single-line sensitivity file.
        float parsedSens = line.toFloat();
        if (parsedSens >= 0.05f && parsedSens <= 1.0f) sensitivity = parsedSens;
        return;
    }

    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim();
    value.trim();

    if (key.equalsIgnoreCase("sensitivity")) {
        float parsedSens = value.toFloat();
        if (parsedSens >= 0.05f && parsedSens <= 1.0f) sensitivity = parsedSens;
    } else if (key.equalsIgnoreCase("invert_x")) {
        invertAxisX = value.toInt() != 0;
    } else if (key.equalsIgnoreCase("invert_y")) {
        invertAxisY = value.toInt() != 0;
    } else if (key.equalsIgnoreCase("left_click")) {
        leftClickBinding = parseBindingLabel(value);
    } else if (key.equalsIgnoreCase("right_click")) {
        rightClickBinding = parseBindingLabel(value);
    } else if (key.equalsIgnoreCase("middle_click")) {
        middleClickBinding = parseBindingLabel(value);
    } else if (key.equalsIgnoreCase("back_click")) {
        backClickBinding = parseBindingLabel(value);
    } else if (key.equalsIgnoreCase("forward_click")) {
        forwardClickBinding = parseBindingLabel(value);
    }
}

void loadAMSettings() {
    sensitivity = 0.15f;
    invertAxisX = false;
    invertAxisY = false;
    leftClickBinding = AM_BIND_ENTER;
    rightClickBinding = AM_BIND_SPACE;
    middleClickBinding = AM_BIND_BTNA;
    backClickBinding = AM_BIND_NONE;
    forwardClickBinding = AM_BIND_NONE;

    if (sdAvailable && SD.exists(amConfigPath)) {
        File file = SD.open(amConfigPath, FILE_READ);
        if (file) {
            while (file.available()) {
                applyConfigLine(file.readStringUntil('\n'));
            }
            file.close();
        }
    }
}

void saveAMSettings() {
    if (!sdAvailable) return;

    if (SD.exists(amConfigPath)) SD.remove(amConfigPath);

    File file = SD.open(amConfigPath, FILE_WRITE);
    if (file) {
        writeConfigLine(file, "sensitivity", String(sensitivity, 2));
        writeConfigLine(file, "invert_x", invertAxisX ? "1" : "0");
        writeConfigLine(file, "invert_y", invertAxisY ? "1" : "0");
        writeConfigLine(file, "left_click", getBindingLabel(leftClickBinding));
        writeConfigLine(file, "right_click", getBindingLabel(rightClickBinding));
        writeConfigLine(file, "middle_click", getBindingLabel(middleClickBinding));
        writeConfigLine(file, "back_click", getBindingLabel(backClickBinding));
        writeConfigLine(file, "forward_click", getBindingLabel(forwardClickBinding));
        file.close();
    }
}

void drawAMBindingSummary(int y) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(LIGHTGREY);
    M5.Display.setCursor(10, y);
    M5.Display.printf("L:%s R:%s M:%s", getBindingLabel(leftClickBinding), getBindingLabel(rightClickBinding), getBindingLabel(middleClickBinding));
    M5.Display.setCursor(10, y + 10);
    M5.Display.printf("B:%s F:%s", getBindingLabel(backClickBinding), getBindingLabel(forwardClickBinding));
}

// Helper function to draw the main operational UI.
void drawAMMainUI() {
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(10, 5);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(GREEN);
    M5.Display.println("Air Mouse BLE v1.2");

    M5.Display.setCursor(10, 28);
    if (bleMouse.isConnected()) {
        M5.Display.setTextColor(BLUE);
        M5.Display.println("Status: Connected");
    } else {
        M5.Display.setTextColor(WHITE);
        M5.Display.println("Status: Waiting");
    }

    M5.Display.setCursor(10, 48);
    M5.Display.setTextColor(ORANGE);
    M5.Display.printf("Sensitivity: %.2f", sensitivity);

    M5.Display.setCursor(10, 66);
    M5.Display.setTextColor(CYAN);
    M5.Display.printf("Invert X:%s  Y:%s", invertAxisX ? "On" : "Off", invertAxisY ? "On" : "Off");

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(DARKGREY);
    M5.Display.setCursor(10, 88);
    M5.Display.println("[m] Menu  [c] Cal  [Del] Back");
    M5.Display.setCursor(10, 98);
    M5.Display.println("[;][.] Wheel  [,][/] H-Wheel");

    drawAMBindingSummary(112);
}

void drawAMMenuRow(int row, int y, const char* label, const String& value) {
    M5.Display.setCursor(10, y);
    M5.Display.setTextColor(amMenuIndex == row ? YELLOW : WHITE);
    M5.Display.printf("%c %s: %s", amMenuIndex == row ? '>' : ' ', label, value.c_str());
}

// Helper function to draw the settings menu.
void drawAMMenu() {
    M5.Display.fillScreen(BLACK);
    M5.Display.setCursor(10, 4);
    M5.Display.setTextColor(CYAN);
    M5.Display.setTextSize(2);
    M5.Display.println("SETTINGS");

    M5.Display.setTextSize(1);
    drawAMMenuRow(AM_MENU_SENSITIVITY, 26, "Sensitivity", String(sensitivity, 2));
    drawAMMenuRow(AM_MENU_INVERT_X, 37, "Invert X", invertAxisX ? "On" : "Off");
    drawAMMenuRow(AM_MENU_INVERT_Y, 48, "Invert Y", invertAxisY ? "On" : "Off");
    drawAMMenuRow(AM_MENU_LEFT_CLICK, 59, "Left Click", getBindingLabel(leftClickBinding));
    drawAMMenuRow(AM_MENU_RIGHT_CLICK, 70, "Right Click", getBindingLabel(rightClickBinding));
    drawAMMenuRow(AM_MENU_MIDDLE_CLICK, 81, "Middle Click", getBindingLabel(middleClickBinding));
    drawAMMenuRow(AM_MENU_BACK_CLICK, 92, "Back Click", getBindingLabel(backClickBinding));
    drawAMMenuRow(AM_MENU_FORWARD_CLICK, 103, "Forward Click", getBindingLabel(forwardClickBinding));
    drawAMMenuRow(AM_MENU_EXIT, 114, "Save & Exit", "");

    M5.Display.setTextColor(DARKGREY);
    M5.Display.setCursor(10, 125);
    M5.Display.printf("Nav:[;/.] Edit:[,/] SD:%s", sdAvailable ? "OK" : "N/A");
}

void updateMenuSetting(int direction) {
    switch (amMenuIndex) {
        case AM_MENU_SENSITIVITY:
            sensitivity = constrain(sensitivity + (0.05f * direction), 0.05f, 1.0f);
            amSettingsChanged = true;
            break;
        case AM_MENU_INVERT_X:
            invertAxisX = !invertAxisX;
            amSettingsChanged = true;
            break;
        case AM_MENU_INVERT_Y:
            invertAxisY = !invertAxisY;
            amSettingsChanged = true;
            break;
        case AM_MENU_LEFT_CLICK:
            leftClickBinding = cycleBinding(leftClickBinding, direction);
            amSettingsChanged = true;
            break;
        case AM_MENU_RIGHT_CLICK:
            rightClickBinding = cycleBinding(rightClickBinding, direction);
            amSettingsChanged = true;
            break;
        case AM_MENU_MIDDLE_CLICK:
            middleClickBinding = cycleBinding(middleClickBinding, direction);
            amSettingsChanged = true;
            break;
        case AM_MENU_BACK_CLICK:
            backClickBinding = cycleBinding(backClickBinding, direction);
            amSettingsChanged = true;
            break;
        case AM_MENU_FORWARD_CLICK:
            forwardClickBinding = cycleBinding(forwardClickBinding, direction);
            amSettingsChanged = true;
            break;
        default:
            break;
    }
}

// Function to calibrate the IMU securely updating the internal state.
void calibrateAMIMU() {
    M5.Display.fillRect(0, 30, 320, 40, BLACK);
    M5.Display.setCursor(10, 30);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(YELLOW);
    M5.Display.println("Calibrating...");
    delay(500);

    float sumGX = 0, sumGY = 0, sumGZ = 0;
    float gX, gY, gZ;
    const int samples = 100;

    for (int i = 0; i < samples; i++) {
        M5Cardputer.update();
        M5.Imu.getGyroData(&gX, &gY, &gZ);
        sumGX += gX;
        sumGY += gY;
        sumGZ += gZ;
        delay(10);
    }
    gyroXOffset = sumGX / samples;
    gyroYOffset = sumGY / samples;
    gyroZOffset = sumGZ / samples;

    fractionX = 0.0f;
    fractionY = 0.0f;
    drawAMMainUI();
}

void updateMouseButtonState(uint8_t mouseButton, AMBindingKey binding, const Keyboard_Class::KeysState& keyState) {
    if (isBindingPressed(binding, keyState)) {
        if (!bleMouse.isPressed(mouseButton)) bleMouse.press(mouseButton);
    } else {
        if (bleMouse.isPressed(mouseButton)) bleMouse.release(mouseButton);
    }
}

// Called once at boot by main.cpp.
void airMouseInit() {
    bleMouse.begin();
}

// Called every time the app is opened from the main menu.
void airMouseResetUI() {
    loadAMSettings();
    amInMenu = false;
    amMenuIndex = 0;
    amSettingsChanged = false;
    fractionX = 0.0f;
    fractionY = 0.0f;
    releaseAllAMButtons();
    drawAMMainUI();
    M5.Imu.begin();
    calibrateAMIMU();
}

// The main logic loop for the Air Mouse.
void airMouseLoop() {
    const Keyboard_Class::KeysState& keyState = M5Cardputer.Keyboard.keysState();
    const unsigned long now = millis();

    if (keyState.del && (now - amLastKeyPress > 250)) {
        if (amSettingsChanged) {
            saveAMSettings();
            amSettingsChanged = false;
        }
        amInMenu = false;
        releaseAllAMButtons();
        returnToMenu = true;
        amLastKeyPress = now;
        return;
    }

    if (amInMenu) {
        if (now - amLastKeyPress > 180) {
            bool redraw = false;

            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                amMenuIndex = (amMenuIndex - 1 + AM_MENU_COUNT) % AM_MENU_COUNT;
                redraw = true;
                amLastKeyPress = now;
            } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                amMenuIndex = (amMenuIndex + 1) % AM_MENU_COUNT;
                redraw = true;
                amLastKeyPress = now;
            } else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
                updateMenuSetting(-1);
                redraw = true;
                amLastKeyPress = now;
            } else if (M5Cardputer.Keyboard.isKeyPressed('/')) {
                updateMenuSetting(1);
                redraw = true;
                amLastKeyPress = now;
            } else if (keyState.enter || M5.BtnA.isPressed()) {
                if (amMenuIndex == AM_MENU_EXIT) {
                    if (amSettingsChanged) {
                        saveAMSettings();
                        amSettingsChanged = false;
                    }
                    amInMenu = false;
                    drawAMMainUI();
                }
                amLastKeyPress = now;
            }

            if (redraw && amInMenu) drawAMMenu();
        }
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('m') && (now - amLastKeyPress > 300)) {
        amInMenu = true;
        amMenuIndex = 0;
        releaseAllAMButtons();
        drawAMMenu();
        amLastKeyPress = now;
        return;
    }

    if ((M5Cardputer.Keyboard.isKeyPressed('c') || M5Cardputer.Keyboard.isKeyPressed('C')) && (now - amLastKeyPress > 300)) {
        releaseAllAMButtons();
        calibrateAMIMU();
        amLastKeyPress = now;
    }

    if (bleMouse.isConnected() != amWasConnected) {
        amWasConnected = bleMouse.isConnected();
        drawAMMainUI();
    }

    if (!bleMouse.isConnected()) return;

    float gyroX, gyroY, gyroZ;
    M5.Imu.getGyroData(&gyroX, &gyroY, &gyroZ);

    gyroX -= gyroXOffset;
    gyroZ -= gyroZOffset;

    const float gyroDeadZone = 5.0f;
    float moveXRaw = 0.0f;
    float moveYRaw = 0.0f;

    if (abs(gyroZ) > gyroDeadZone) moveXRaw = -gyroZ * sensitivity;
    if (abs(gyroX) > gyroDeadZone) moveYRaw = gyroX * sensitivity;

    if (invertAxisX) moveXRaw = -moveXRaw;
    if (invertAxisY) moveYRaw = -moveYRaw;

    fractionX += moveXRaw;
    fractionY += moveYRaw;

    const signed char sendX = (signed char)constrain((int)fractionX, -127, 127);
    const signed char sendY = (signed char)constrain((int)fractionY, -127, 127);

    fractionX -= sendX;
    fractionY -= sendY;

    signed char wheel = 0;
    signed char hWheel = 0;
    if (now - amLastScrollTick >= 80) {
        if (M5Cardputer.Keyboard.isKeyPressed(';')) wheel = 1;
        else if (M5Cardputer.Keyboard.isKeyPressed('.')) wheel = -1;

        if (M5Cardputer.Keyboard.isKeyPressed(',')) hWheel = -1;
        else if (M5Cardputer.Keyboard.isKeyPressed('/')) hWheel = 1;

        if (wheel != 0 || hWheel != 0) amLastScrollTick = now;
    }

    if (sendX != 0 || sendY != 0 || wheel != 0 || hWheel != 0) {
        bleMouse.move(sendX, sendY, wheel, hWheel);
    }

    updateMouseButtonState(MOUSE_LEFT, leftClickBinding, keyState);
    updateMouseButtonState(MOUSE_RIGHT, rightClickBinding, keyState);
    updateMouseButtonState(MOUSE_MIDDLE, middleClickBinding, keyState);
    updateMouseButtonState(MOUSE_BACK, backClickBinding, keyState);
    updateMouseButtonState(MOUSE_FORWARD, forwardClickBinding, keyState);
}
