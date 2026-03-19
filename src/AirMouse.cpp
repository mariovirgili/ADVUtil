#include "AirMouse.h"

#include "BleComboHID.h"

#include <M5Cardputer.h>
#include <SD.h>

// Import the shared SD/menu state from main.cpp.
extern bool sdAvailable;
extern bool returnToMenu;

BleComboHID bleCombo("Cardputer Mouse", "M5Stack", 100);

const char* amConfigPath = "/ADVUtil/airmouse.cfg";
bool amSettingsChanged = false;

float gyroXOffset = 0.0f;
float gyroYOffset = 0.0f;
float gyroZOffset = 0.0f;

float fractionX = 0.0f;
float fractionY = 0.0f;
float sensitivity = 0.15f;
bool invertAxisX = false;
bool invertAxisY = true;

enum AMBindingKey : uint8_t {
    AM_BIND_NONE = 0,
    AM_BIND_ENTER,
    AM_BIND_SPACE,
    AM_BIND_Z,
    AM_BIND_X,
    AM_BIND_V,
    AM_BIND_B,
    AM_BIND_N,
    AM_BIND_COUNT
};

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

enum AMControlMode : uint8_t {
    AM_MODE_MOUSE = 0,
    AM_MODE_KEYBOARD
};

AMBindingKey leftClickBinding = AM_BIND_ENTER;
AMBindingKey rightClickBinding = AM_BIND_SPACE;
AMBindingKey middleClickBinding = AM_BIND_V;
AMBindingKey backClickBinding = AM_BIND_NONE;
AMBindingKey forwardClickBinding = AM_BIND_NONE;

bool amInMenu = false;
int amMenuIndex = 0;
unsigned long amLastKeyPress = 0;
unsigned long amLastScrollTick = 0;
bool amWasConnected = false;
bool amDelWasPressed = false;
bool amExitArmed = false;
unsigned long amExitArmMillis = 0;
AMControlMode amControlMode = AM_MODE_MOUSE;
String amLastKeyboardPreview = "";
bool amLastKeyboardHintVisible = false;
bool amShowMouseHelp = false;

constexpr unsigned long AM_EXIT_DOUBLE_TAP_MS = 550;
constexpr uint8_t AM_HID_RIGHT_ARROW = 0x4F;
constexpr uint8_t AM_HID_LEFT_ARROW = 0x50;
constexpr uint8_t AM_HID_DOWN_ARROW = 0x51;
constexpr uint8_t AM_HID_UP_ARROW = 0x52;

const char* getBindingLabel(AMBindingKey binding) {
    switch (binding) {
        case AM_BIND_NONE:  return "None";
        case AM_BIND_ENTER: return "Enter";
        case AM_BIND_SPACE: return "Space";
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
    if (value.equalsIgnoreCase("btna"))  return AM_BIND_V;
    if (value.equalsIgnoreCase("z"))     return AM_BIND_Z;
    if (value.equalsIgnoreCase("x"))     return AM_BIND_X;
    if (value.equalsIgnoreCase("v"))     return AM_BIND_V;
    if (value.equalsIgnoreCase("b"))     return AM_BIND_B;
    if (value.equalsIgnoreCase("n"))     return AM_BIND_N;
    return AM_BIND_NONE;
}

AMBindingKey cycleBinding(AMBindingKey current, int direction) {
    int next = static_cast<int>(current) + direction;
    if (next < 0) next = AM_BIND_COUNT - 1;
    if (next >= AM_BIND_COUNT) next = 0;
    return static_cast<AMBindingKey>(next);
}

bool isAlphaKeyPressed(char lower, char upper) {
    return M5Cardputer.Keyboard.isKeyPressed(lower) || M5Cardputer.Keyboard.isKeyPressed(upper);
}

bool isBindingPressed(AMBindingKey binding, const Keyboard_Class::KeysState& keyState) {
    switch (binding) {
        case AM_BIND_NONE:  return false;
        case AM_BIND_ENTER: return keyState.enter;
        case AM_BIND_SPACE: return keyState.space;
        case AM_BIND_Z:     return isAlphaKeyPressed('z', 'Z');
        case AM_BIND_X:     return isAlphaKeyPressed('x', 'X');
        case AM_BIND_V:     return isAlphaKeyPressed('v', 'V');
        case AM_BIND_B:     return isAlphaKeyPressed('b', 'B');
        case AM_BIND_N:     return isAlphaKeyPressed('n', 'N');
        default:            return false;
    }
}

void releaseAllAMButtons() {
    bleCombo.releaseAllMouseButtons();
    bleCombo.releaseKeyboard();
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
    invertAxisY = true;
    leftClickBinding = AM_BIND_ENTER;
    rightClickBinding = AM_BIND_SPACE;
    middleClickBinding = AM_BIND_V;
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

uint16_t amGradientColor(int step, int totalSteps) {
    const int r0 = 100, g0 = 200, b0 = 255;
    const int r1 = 0, g1 = 70, b1 = 150;
    const int r = r0 + (r1 - r0) * step / totalSteps;
    const int g = g0 + (g1 - g0) * step / totalSteps;
    const int b = b0 + (b1 - b0) * step / totalSteps;
    return M5.Display.color565(r, g, b);
}

void drawAMBackground() {
    for (int y = 0; y < M5.Display.height(); ++y) {
        const uint16_t color = amGradientColor(y, M5.Display.height());
        M5.Display.drawGradientLine(0, y, M5.Display.width(), y, color, color);
    }
}

void drawAMHeader(const char* title, const char* subtitle) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE);
    const int titleX = (M5.Display.width() - (strlen(title) * 6)) / 2;
    M5.Display.setCursor(titleX, 10);
    M5.Display.println(title);
    if (subtitle != nullptr && subtitle[0] != '\0') {
        M5.Display.setTextColor(M5.Display.color565(220, 245, 255));
        M5.Display.setCursor(12, 21);
        M5.Display.println(subtitle);
    }
}

void drawAMCard(int x, int y, int w, int h, bool selected) {
    const uint16_t fill = selected ? WHITE : M5.Display.color565(7, 37, 79);
    const uint16_t border = selected ? WHITE : M5.Display.color565(170, 230, 255);
    M5.Display.fillRoundRect(x, y, w, h, 10, fill);
    M5.Display.drawRoundRect(x, y, w, h, 10, border);
}

void drawAMCardTitle(int x, int y, const char* text, bool selected) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(selected ? BLACK : M5.Display.color565(180, 235, 255));
    M5.Display.setCursor(x, y);
    M5.Display.println(text);
}

void drawAMCardValue(int x, int y, const String& text, bool selected) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(selected ? BLACK : WHITE);
    M5.Display.setCursor(x, y);
    M5.Display.println(text);
}

String getConnectionText() {
    return bleCombo.isConnected() ? "Connected" : "Waiting pair";
}

String getModeText() {
    return amControlMode == AM_MODE_MOUSE ? "Mouse" : "Keyboard";
}

const char* getFnArrowLabel(char key) {
    switch (key) {
        case ';': return "Up";
        case ',': return "Left";
        case '.': return "Down";
        case '/': return "Right";
        default:  return nullptr;
    }
}

uint8_t remapFnArrowKey(const Keyboard_Class::KeysState& keyState, uint8_t hidKey) {
    if (!keyState.fn) return hidKey;

    switch (hidKey) {
        case 0x33: return AM_HID_UP_ARROW;
        case 0x36: return AM_HID_LEFT_ARROW;
        case 0x37: return AM_HID_DOWN_ARROW;
        case 0x38: return AM_HID_RIGHT_ARROW;
        default:   return hidKey;
    }
}

String getPreviewText(const Keyboard_Class::KeysState& keyState) {
    if (keyState.hid_keys.empty() && keyState.modifiers == 0) return "Ready to type";

    String preview = "";
    if (keyState.ctrl) preview += "Ctrl ";
    if (keyState.shift) preview += "Shift ";
    if (keyState.alt) preview += "Alt ";

    for (char c : keyState.word) {
        if (preview.length() >= 24) break;
        if (preview.length() > 0) preview += ' ';
        if (keyState.fn) {
            const char* arrowLabel = getFnArrowLabel(c);
            if (arrowLabel != nullptr) {
                preview += arrowLabel;
                continue;
            }
        }
        if (c == ' ') preview += "Space";
        else preview += c;
    }

    if (preview.length() == 0) {
        if (keyState.enter) preview = "Enter";
        else if (keyState.del) preview = "Backspace";
        else if (keyState.tab) preview = "Tab";
    }

    return preview;
}

bool hasExitHint(unsigned long now) {
    return amExitArmed && (now - amExitArmMillis) <= AM_EXIT_DOUBLE_TAP_MS;
}

void drawAMFooter(const String& leftText, const String& rightText, uint16_t color = WHITE) {
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(color);
    M5.Display.setCursor(12, 122);
    M5.Display.print(leftText);

    const int rightX = M5.Display.width() - (rightText.length() * 6) - 12;
    M5.Display.setCursor(rightX, 122);
    M5.Display.print(rightText);
}

void drawAMMouseHelpOverlay() {
    M5.Display.fillRoundRect(12, 18, 216, 102, 10, M5.Display.color565(7, 37, 79));
    M5.Display.drawRoundRect(12, 18, 216, 102, 10, WHITE);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(24, 28);
    M5.Display.println("Mouse Help");

    M5.Display.setTextColor(M5.Display.color565(220, 245, 255));
    M5.Display.setCursor(24, 42);
    M5.Display.println("M settings   C calibrate   H close");
    M5.Display.setCursor(24, 54);
    M5.Display.println("BtnG0 switch mouse/keyboard");
    M5.Display.setCursor(24, 66);
    M5.Display.println("Del Del exit to main menu");
    M5.Display.setCursor(24, 78);
    M5.Display.println("Wheel ;/.   Pan ,/");
    M5.Display.setCursor(24, 90);
    M5.Display.printf("L:%s  R:%s  M:%s", getBindingLabel(leftClickBinding), getBindingLabel(rightClickBinding), getBindingLabel(middleClickBinding));
    M5.Display.setCursor(24, 102);
    M5.Display.printf("Back:%s  Fwd:%s", getBindingLabel(backClickBinding), getBindingLabel(forwardClickBinding));
}

void drawAMMainUI(const Keyboard_Class::KeysState* keyState = nullptr) {
    drawAMBackground();

    if (amControlMode == AM_MODE_MOUSE) {
        drawAMHeader("Air Mouse BLE   BLE combo ready", "");

        drawAMCard(10, 30, 104, 28, false);
        drawAMCardTitle(20, 37, "Status", false);
        drawAMCardValue(20, 47, getConnectionText(), false);

        drawAMCard(126, 30, 104, 28, false);
        drawAMCardTitle(136, 37, "Mode", false);
        drawAMCardValue(136, 47, getModeText(), false);

        drawAMCard(10, 64, 104, 38, false);
        drawAMCardTitle(20, 71, "Motion", false);
        M5.Display.setTextColor(WHITE);
        M5.Display.setCursor(20, 81);
        M5.Display.printf("Sensi %.2f", sensitivity);
        M5.Display.setCursor(20, 91);
        M5.Display.printf("X:%s  Y:%s", invertAxisX ? "On" : "Off", invertAxisY ? "On" : "Off");

        drawAMCard(126, 64, 104, 38, false);
        drawAMCardTitle(136, 71, "Actions", false);
        drawAMCardValue(136, 81, "M settings", false);
        drawAMCardValue(136, 91, "C calibrate", false);

        drawAMCard(10, 104, 220, 12, false);
        drawAMCardTitle(20, 107, "Top button swaps mode", false);

        if (amShowMouseHelp) {
            drawAMMouseHelpOverlay();
        }

        if (hasExitHint(millis())) {
            drawAMFooter("Del Del exit", "H for Help", TFT_YELLOW);
        } else {
            drawAMFooter("BtnG0 to Keys", "H for Help", M5.Display.color565(220, 245, 255));
        }
        return;
    }

    drawAMHeader("Keyboard BLE", "BLE keyboard passthrough");

    drawAMCard(10, 44, 104, 28, false);
    drawAMCardTitle(20, 51, "Status", false);
    drawAMCardValue(20, 61, getConnectionText(), false);

    drawAMCard(126, 44, 104, 28, false);
    drawAMCardTitle(136, 51, "Mode", false);
    drawAMCardValue(136, 61, getModeText(), false);

    drawAMCard(10, 80, 220, 30, false);
    drawAMCardTitle(20, 87, "Typing", false);
    drawAMCardValue(20, 98, keyState ? getPreviewText(*keyState) : String("Ready to type"), false);

    drawAMFooter("Del = backspace", "BtnG0 -> Mouse", M5.Display.color565(220, 245, 255));
}

void drawAMMenuRow(int y, const char* label, const String& value, bool selected) {
    drawAMCard(10, y - 6, 220, 14, selected);
    drawAMCardTitle(18, y - 2, label, selected);

    const int valueX = 224 - (value.length() * 6);
    drawAMCardValue(valueX, y - 2, value, selected);
}

void drawAMMenu() {
    drawAMBackground();
    drawAMHeader("Air Mouse", "Settings");

    const int visibleRows = 6;
    int start = amMenuIndex - (visibleRows / 2);
    if (start < 0) start = 0;
    if (start > AM_MENU_COUNT - visibleRows) start = AM_MENU_COUNT - visibleRows;
    if (start < 0) start = 0;

    const char* labels[AM_MENU_COUNT] = {
        "Sensitivity",
        "Invert X",
        "Invert Y",
        "Left Click",
        "Right Click",
        "Middle Click",
        "Back Click",
        "Forward Click",
        "Save & Exit"
    };

    String values[AM_MENU_COUNT] = {
        String(sensitivity, 2),
        invertAxisX ? "On" : "Off",
        invertAxisY ? "On" : "Off",
        getBindingLabel(leftClickBinding),
        getBindingLabel(rightClickBinding),
        getBindingLabel(middleClickBinding),
        getBindingLabel(backClickBinding),
        getBindingLabel(forwardClickBinding),
        ""
    };

    int y = 46;
    for (int i = start; i < AM_MENU_COUNT && i < start + visibleRows; ++i) {
        drawAMMenuRow(y, labels[i], values[i], i == amMenuIndex);
        y += 13;
    }

    if (hasExitHint(millis())) {
        drawAMFooter("Del again to exit", "Ent save  ;/. nav", TFT_YELLOW);
    } else {
        drawAMFooter("Ent save  ;/. nav", ",/ edit", M5.Display.color565(220, 245, 255));
    }
}

void refreshAMUI(const Keyboard_Class::KeysState* keyState = nullptr) {
    if (amInMenu) drawAMMenu();
    else drawAMMainUI(keyState);
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

void calibrateAMIMU() {
    M5.Display.fillRoundRect(38, 44, 164, 48, 10, M5.Display.color565(7, 37, 79));
    M5.Display.drawRoundRect(38, 44, 164, 48, 10, WHITE);
    M5.Display.setCursor(66, 60);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(YELLOW);
    M5.Display.println("CAL...");
    delay(500);

    float sumGX = 0, sumGY = 0, sumGZ = 0;
    float gX, gY, gZ;
    const int samples = 100;

    for (int i = 0; i < samples; ++i) {
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
    refreshAMUI();
}

void updateMouseButtonState(uint8_t mouseButton, AMBindingKey binding, const Keyboard_Class::KeysState& keyState) {
    if (isBindingPressed(binding, keyState)) {
        if (!bleCombo.isMousePressed(mouseButton)) bleCombo.pressMouse(mouseButton);
    } else {
        if (bleCombo.isMousePressed(mouseButton)) bleCombo.releaseMouse(mouseButton);
    }
}

void clearExitArm() {
    amExitArmed = false;
}

void closeAirMouseMenu() {
    if (amSettingsChanged) {
        saveAMSettings();
        amSettingsChanged = false;
    }
    amInMenu = false;
    clearExitArm();
    releaseAllAMButtons();
    refreshAMUI();
}

void exitAirMouse() {
    if (amSettingsChanged) {
        saveAMSettings();
        amSettingsChanged = false;
    }
    amInMenu = false;
    clearExitArm();
    releaseAllAMButtons();
    returnToMenu = true;
}

bool handleExitGesture(const Keyboard_Class::KeysState& keyState, unsigned long now) {
    if (amControlMode == AM_MODE_KEYBOARD && !amInMenu) {
        amDelWasPressed = keyState.del;
        return false;
    }

    const bool delPressed = keyState.del;
    const bool delEdge = delPressed && !amDelWasPressed;
    amDelWasPressed = delPressed;

    if (!delEdge) return false;

    if (amExitArmed && (now - amExitArmMillis) <= AM_EXIT_DOUBLE_TAP_MS) {
        if (amInMenu) {
            closeAirMouseMenu();
            return true;
        }
        exitAirMouse();
        return true;
    }

    amExitArmed = true;
    amExitArmMillis = now;
    releaseAllAMButtons();
    refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
    return true;
}

void flushPendingExitAction(const Keyboard_Class::KeysState& keyState, unsigned long now) {
    if (amControlMode == AM_MODE_KEYBOARD && !amInMenu) {
        clearExitArm();
        return;
    }

    if (!amExitArmed) return;

    if ((now - amExitArmMillis) <= AM_EXIT_DOUBLE_TAP_MS) return;
    clearExitArm();
    refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
}

void toggleControlMode(const Keyboard_Class::KeysState& keyState) {
    clearExitArm();
    releaseAllAMButtons();
    amControlMode = (amControlMode == AM_MODE_MOUSE) ? AM_MODE_KEYBOARD : AM_MODE_MOUSE;
    amShowMouseHelp = false;
    amLastKeyboardPreview = "";
    refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
}

void handleKeyboardMode(const Keyboard_Class::KeysState& keyState) {
    uint8_t keys[6] = {0};
    size_t keyCount = 0;

    for (uint8_t hidKey : keyState.hid_keys) {
        if (keyCount < 6) keys[keyCount++] = remapFnArrowKey(keyState, hidKey);
    }

    bleCombo.sendKeyboardReport(keyState.modifiers, keys, keyCount);

    if (M5Cardputer.Keyboard.isChange()) {
        const String preview = getPreviewText(keyState);
        if (preview != amLastKeyboardPreview || hasExitHint(millis()) != amLastKeyboardHintVisible) {
            amLastKeyboardPreview = preview;
            amLastKeyboardHintVisible = hasExitHint(millis());
            refreshAMUI(&keyState);
        }
    }
}

void handleMouseMode(const Keyboard_Class::KeysState& keyState, unsigned long now) {
    if (M5Cardputer.Keyboard.isKeyPressed('m') && (now - amLastKeyPress > 300)) {
        amInMenu = true;
        amMenuIndex = 0;
        amShowMouseHelp = false;
        releaseAllAMButtons();
        refreshAMUI();
        amLastKeyPress = now;
        return;
    }

    if ((M5Cardputer.Keyboard.isKeyPressed('h') || M5Cardputer.Keyboard.isKeyPressed('H')) && (now - amLastKeyPress > 300)) {
        amShowMouseHelp = !amShowMouseHelp;
        refreshAMUI();
        amLastKeyPress = now;
        return;
    }

    if ((M5Cardputer.Keyboard.isKeyPressed('c') || M5Cardputer.Keyboard.isKeyPressed('C')) && (now - amLastKeyPress > 300)) {
        releaseAllAMButtons();
        calibrateAMIMU();
        amLastKeyPress = now;
    }

    if (!bleCombo.isConnected()) return;

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

    const signed char sendX = static_cast<signed char>(constrain(static_cast<int>(fractionX), -127, 127));
    const signed char sendY = static_cast<signed char>(constrain(static_cast<int>(fractionY), -127, 127));

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
        bleCombo.move(sendX, sendY, wheel, hWheel);
    }

    updateMouseButtonState(COMBO_MOUSE_LEFT, leftClickBinding, keyState);
    updateMouseButtonState(COMBO_MOUSE_RIGHT, rightClickBinding, keyState);
    updateMouseButtonState(COMBO_MOUSE_MIDDLE, middleClickBinding, keyState);
    updateMouseButtonState(COMBO_MOUSE_BACK, backClickBinding, keyState);
    updateMouseButtonState(COMBO_MOUSE_FORWARD, forwardClickBinding, keyState);
}

void handleSettingsMenu(unsigned long now) {
    if (now - amLastKeyPress <= 180) return;

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
    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) || M5.BtnA.isPressed()) {
        if (amMenuIndex == AM_MENU_EXIT) {
            closeAirMouseMenu();
        }
        amLastKeyPress = now;
    }

    if (redraw && amInMenu) refreshAMUI();
}

void airMouseInit() {
    bleCombo.begin();
}

void airMouseResetUI() {
    loadAMSettings();
    amInMenu = false;
    amMenuIndex = 0;
    amSettingsChanged = false;
    amControlMode = AM_MODE_MOUSE;
    amShowMouseHelp = false;
    amWasConnected = bleCombo.isConnected();
    amDelWasPressed = false;
    clearExitArm();
    fractionX = 0.0f;
    fractionY = 0.0f;
    amLastKeyboardPreview = "";
    amLastKeyboardHintVisible = false;
    releaseAllAMButtons();
    refreshAMUI();
    M5.Imu.begin();
    calibrateAMIMU();
}

void airMouseLoop() {
    const Keyboard_Class::KeysState& keyState = M5Cardputer.Keyboard.keysState();
    const unsigned long now = millis();

    flushPendingExitAction(keyState, now);
    if (handleExitGesture(keyState, now)) return;

    if (bleCombo.isConnected() != amWasConnected) {
        amWasConnected = bleCombo.isConnected();
        refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
    }

    if (!amInMenu && M5Cardputer.BtnA.wasPressed()) {
        toggleControlMode(keyState);
        return;
    }

    if (amInMenu) {
        handleSettingsMenu(now);
        return;
    }

    if (amControlMode == AM_MODE_KEYBOARD) {
        handleKeyboardMode(keyState);
        return;
    }

    handleMouseMode(keyState, now);
}
