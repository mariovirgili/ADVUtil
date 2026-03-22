#include "Gamepad.h"

#include "DisplayBrightness.h"

#include <M5Cardputer.h>
#include <SD.h>
#include <BleGamepad.h>

extern bool sdAvailable;
extern bool returnToMenu;

namespace {

BleGamepad bleGamepad("Cardputer Gamepad", "M5Stack", 100);
M5Canvas* gpCanvas = nullptr;
constexpr const char* kGamepadConfigPath = "/ADVUtil/gamepad.cfg";
constexpr unsigned long GP_EXIT_DOUBLE_TAP_MS = 550;
constexpr unsigned long GP_BLE_START_DELAY_MS = 250;
constexpr int16_t GP_AXIS_MAX = 32767;
constexpr int16_t GP_AXIS_MIN = -32767;
constexpr uint8_t GP_BUTTON_COUNT = 8;
constexpr float GP_MOTION_TILT_THRESHOLD_DEG = 11.0f;

enum GPBindingKey : uint8_t {
    GP_BIND_NONE = 0,
    GP_BIND_ENTER,
    GP_BIND_SPACE,
    GP_BIND_Z,
    GP_BIND_X,
    GP_BIND_Q,
    GP_BIND_W,
    GP_BIND_E,
    GP_BIND_R,
    GP_BIND_1,
    GP_BIND_2,
    GP_BIND_4,
    GP_BIND_TAB,
    GP_BIND_COUNT
};

enum GPProfile : uint8_t {
    GP_PROFILE_TWIN_STICK = 0,
    GP_PROFILE_SOUTHPAW,
    GP_PROFILE_COUNT
};

enum GPMoveInput : uint8_t {
    GP_MOVE_KEYS = 0,
    GP_MOVE_MOTION,
    GP_MOVE_COUNT
};

enum GPMenuItem : uint8_t {
    GP_MENU_PROFILE = 0,
    GP_MENU_MOVE_INPUT,
    GP_MENU_MOTION_SENS,
    GP_MENU_INVERT_X,
    GP_MENU_INVERT_Y,
    GP_MENU_BRIGHTNESS,
    GP_MENU_A_BUTTON,
    GP_MENU_B_BUTTON,
    GP_MENU_X_BUTTON,
    GP_MENU_Y_BUTTON,
    GP_MENU_L1_BUTTON,
    GP_MENU_R1_BUTTON,
    GP_MENU_BACK_BUTTON,
    GP_MENU_START_BUTTON,
    GP_MENU_EXIT,
    GP_MENU_COUNT
};

struct GPReportState {
    int16_t leftX = 0;
    int16_t leftY = 0;
    int16_t rightX = 0;
    int16_t rightY = 0;
    signed char hat = DPAD_CENTERED;
    bool buttons[GP_BUTTON_COUNT] = {false, false, false, false, false, false, false, false};
};

bool gpSettingsChanged = false;
bool gpInMenu = false;
bool gpShowHelp = false;
bool gpWasConnected = false;
bool gpDelWasPressed = false;
bool gpExitArmed = false;
bool gpBleStarted = false;
bool gpHasLastReport = false;
bool gpImuReady = false;
bool gpBrightnessMuted = false;
bool gpBrightnessShortcutPressed = false;
int gpMenuIndex = 0;
unsigned long gpLastKeyPress = 0;
unsigned long gpExitArmMillis = 0;
unsigned long gpModeStartMillis = 0;

GPProfile gpProfile = GP_PROFILE_TWIN_STICK;
GPMoveInput gpMoveInput = GP_MOVE_KEYS;
uint8_t gpBrightnessLevel = kDisplayBrightnessDefaultLevel;
GPBindingKey gpABinding = GP_BIND_ENTER;
GPBindingKey gpBBinding = GP_BIND_SPACE;
GPBindingKey gpXBinding = GP_BIND_Z;
GPBindingKey gpYBinding = GP_BIND_X;
GPBindingKey gpL1Binding = GP_BIND_W;
GPBindingKey gpR1Binding = GP_BIND_R;
GPBindingKey gpBackBinding = GP_BIND_2;
GPBindingKey gpStartBinding = GP_BIND_4;
GPReportState gpLastReport;
float gpPitchOffset = 0.0f;
float gpRollOffset = 0.0f;
float gpMotionSensitivity = 1.0f;
bool gpInvertMotionX = false;
bool gpInvertMotionY = true;

uint16_t gpGradientColor(int step, int totalSteps) {
    const int r0 = 100;
    const int g0 = 200;
    const int b0 = 255;
    const int r1 = 0;
    const int g1 = 70;
    const int b1 = 150;
    const int r = r0 + (r1 - r0) * step / totalSteps;
    const int g = g0 + (g1 - g0) * step / totalSteps;
    const int b = b0 + (b1 - b0) * step / totalSteps;
    return gpCanvas->color565(r, g, b);
}

void drawGPBackground() {
    for (int y = 0; y < gpCanvas->height(); ++y) {
        const uint16_t color = gpGradientColor(y, gpCanvas->height());
        gpCanvas->drawGradientLine(0, y, gpCanvas->width(), y, color, color);
    }
}

void drawGPHeader(const char* title, const char* subtitle) {
    gpCanvas->setTextSize(1);
    gpCanvas->setTextColor(WHITE);
    const int titleX = (gpCanvas->width() - (strlen(title) * 6)) / 2;
    gpCanvas->setCursor(titleX, 10);
    gpCanvas->println(title);
    if (subtitle != nullptr && subtitle[0] != '\0') {
        gpCanvas->setTextColor(gpCanvas->color565(220, 245, 255));
        gpCanvas->setCursor(12, 21);
        gpCanvas->println(subtitle);
    }
}

void drawGPCard(int x, int y, int w, int h, bool selected) {
    const uint16_t fill = selected ? WHITE : gpCanvas->color565(7, 37, 79);
    const uint16_t border = selected ? WHITE : gpCanvas->color565(170, 230, 255);
    gpCanvas->fillRoundRect(x, y, w, h, 10, fill);
    gpCanvas->drawRoundRect(x, y, w, h, 10, border);
}

void drawGPCardTitle(int x, int y, const char* text, bool selected) {
    gpCanvas->setTextSize(1);
    gpCanvas->setTextColor(selected ? BLACK : gpCanvas->color565(180, 235, 255));
    gpCanvas->setCursor(x, y);
    gpCanvas->println(text);
}

void drawGPCardValue(int x, int y, const String& text, bool selected) {
    gpCanvas->setTextSize(1);
    gpCanvas->setTextColor(selected ? BLACK : WHITE);
    gpCanvas->setCursor(x, y);
    gpCanvas->println(text);
}

void drawGPFooter(const String& leftText, const String& rightText, uint16_t color = WHITE) {
    gpCanvas->setTextSize(1);
    gpCanvas->setTextColor(color);
    gpCanvas->setCursor(12, 122);
    gpCanvas->print(leftText);

    const int rightX = gpCanvas->width() - (rightText.length() * 6) - 12;
    gpCanvas->setCursor(rightX, 122);
    gpCanvas->print(rightText);
}

void syncGPDisplayBrightness() {
    applyDisplayBrightnessLevel(gpBrightnessMuted ? 0 : gpBrightnessLevel);
}

const char* getGPProfileLabel(GPProfile profile) {
    switch (profile) {
        case GP_PROFILE_TWIN_STICK: return "Twin Stick";
        case GP_PROFILE_SOUTHPAW:   return "Southpaw";
        default:                    return "Twin Stick";
    }
}

const char* getGPProfileConfigValue(GPProfile profile) {
    switch (profile) {
        case GP_PROFILE_TWIN_STICK: return "twin_stick";
        case GP_PROFILE_SOUTHPAW:   return "southpaw";
        default:                    return "twin_stick";
    }
}

GPProfile parseGPProfile(const String& value) {
    if (value.equalsIgnoreCase("southpaw")) return GP_PROFILE_SOUTHPAW;
    return GP_PROFILE_TWIN_STICK;
}

GPProfile cycleGPProfile(GPProfile profile, int direction) {
    int next = static_cast<int>(profile) + direction;
    if (next < 0) next = GP_PROFILE_COUNT - 1;
    if (next >= GP_PROFILE_COUNT) next = 0;
    return static_cast<GPProfile>(next);
}

const char* getGPMoveInputLabel(GPMoveInput moveInput) {
    return moveInput == GP_MOVE_MOTION ? "Motion" : "Keys";
}

const char* getGPMoveInputConfigValue(GPMoveInput moveInput) {
    return moveInput == GP_MOVE_MOTION ? "motion" : "keys";
}

bool parseGPBool(const String& value, bool fallback) {
    if (value.equalsIgnoreCase("1") || value.equalsIgnoreCase("true") ||
        value.equalsIgnoreCase("yes") || value.equalsIgnoreCase("on")) {
        return true;
    }
    if (value.equalsIgnoreCase("0") || value.equalsIgnoreCase("false") ||
        value.equalsIgnoreCase("no") || value.equalsIgnoreCase("off")) {
        return false;
    }
    return fallback;
}

const char* getGPOnOffLabel(bool enabled) {
    return enabled ? "On" : "Off";
}

GPMoveInput parseGPMoveInput(const String& value) {
    return value.equalsIgnoreCase("motion") ? GP_MOVE_MOTION : GP_MOVE_KEYS;
}

GPMoveInput cycleGPMoveInput(GPMoveInput moveInput, int direction) {
    int next = static_cast<int>(moveInput) + direction;
    if (next < 0) next = GP_MOVE_COUNT - 1;
    if (next >= GP_MOVE_COUNT) next = 0;
    return static_cast<GPMoveInput>(next);
}

bool isAlphaKeyPressed(char lower, char upper) {
    return M5Cardputer.Keyboard.isKeyPressed(lower) || M5Cardputer.Keyboard.isKeyPressed(upper);
}

const char* getGPBindingLabel(GPBindingKey binding) {
    switch (binding) {
        case GP_BIND_NONE:  return "None";
        case GP_BIND_ENTER: return "Enter";
        case GP_BIND_SPACE: return "Space";
        case GP_BIND_Z:     return "Z";
        case GP_BIND_X:     return "X";
        case GP_BIND_Q:     return "Q";
        case GP_BIND_W:     return "W";
        case GP_BIND_E:     return "E";
        case GP_BIND_R:     return "R";
        case GP_BIND_1:     return "1";
        case GP_BIND_2:     return "2";
        case GP_BIND_4:     return "4";
        case GP_BIND_TAB:   return "Tab";
        default:            return "Enter";
    }
}

const char* getGPBindingShortLabel(GPBindingKey binding) {
    switch (binding) {
        case GP_BIND_NONE:  return "-";
        case GP_BIND_ENTER: return "Ent";
        case GP_BIND_SPACE: return "Spc";
        case GP_BIND_Z:     return "Z";
        case GP_BIND_X:     return "X";
        case GP_BIND_Q:     return "Q";
        case GP_BIND_W:     return "W";
        case GP_BIND_E:     return "E";
        case GP_BIND_R:     return "R";
        case GP_BIND_1:     return "1";
        case GP_BIND_2:     return "2";
        case GP_BIND_4:     return "4";
        case GP_BIND_TAB:   return "Tab";
        default:            return "Ent";
    }
}

GPBindingKey parseGPBinding(const String& value) {
    if (value.equalsIgnoreCase("none"))  return GP_BIND_NONE;
    if (value.equalsIgnoreCase("enter")) return GP_BIND_ENTER;
    if (value.equalsIgnoreCase("space")) return GP_BIND_SPACE;
    if (value.equalsIgnoreCase("z"))     return GP_BIND_Z;
    if (value.equalsIgnoreCase("x"))     return GP_BIND_X;
    if (value.equalsIgnoreCase("q"))     return GP_BIND_Q;
    if (value.equalsIgnoreCase("w"))     return GP_BIND_W;
    if (value.equalsIgnoreCase("e"))     return GP_BIND_E;
    if (value.equalsIgnoreCase("r"))     return GP_BIND_R;
    if (value.equalsIgnoreCase("1"))     return GP_BIND_1;
    if (value.equalsIgnoreCase("2"))     return GP_BIND_2;
    if (value.equalsIgnoreCase("4"))     return GP_BIND_4;
    if (value.equalsIgnoreCase("tab"))   return GP_BIND_TAB;
    return GP_BIND_ENTER;
}

GPBindingKey cycleGPBinding(GPBindingKey binding, int direction) {
    int next = static_cast<int>(binding) + direction;
    if (next < 0) next = GP_BIND_COUNT - 1;
    if (next >= GP_BIND_COUNT) next = 0;
    return static_cast<GPBindingKey>(next);
}

bool isGPBindingPressed(GPBindingKey binding, const Keyboard_Class::KeysState& keyState) {
    switch (binding) {
        case GP_BIND_NONE:  return false;
        case GP_BIND_ENTER: return keyState.enter;
        case GP_BIND_SPACE: return keyState.space;
        case GP_BIND_Z:     return isAlphaKeyPressed('z', 'Z');
        case GP_BIND_X:     return isAlphaKeyPressed('x', 'X');
        case GP_BIND_Q:     return isAlphaKeyPressed('q', 'Q');
        case GP_BIND_W:     return isAlphaKeyPressed('w', 'W');
        case GP_BIND_E:     return isAlphaKeyPressed('e', 'E');
        case GP_BIND_R:     return isAlphaKeyPressed('r', 'R');
        case GP_BIND_1:     return M5Cardputer.Keyboard.isKeyPressed('1');
        case GP_BIND_2:     return M5Cardputer.Keyboard.isKeyPressed('2');
        case GP_BIND_4:     return M5Cardputer.Keyboard.isKeyPressed('4');
        case GP_BIND_TAB:   return keyState.tab;
        default:            return false;
    }
}

void loadGPDefaults() {
    gpProfile = GP_PROFILE_TWIN_STICK;
    gpMoveInput = GP_MOVE_KEYS;
    gpMotionSensitivity = 1.0f;
    gpInvertMotionX = false;
    gpInvertMotionY = true;
    gpBrightnessLevel = kDisplayBrightnessDefaultLevel;
    gpBrightnessMuted = false;
    gpABinding = GP_BIND_ENTER;
    gpBBinding = GP_BIND_SPACE;
    gpXBinding = GP_BIND_Z;
    gpYBinding = GP_BIND_X;
    gpL1Binding = GP_BIND_W;
    gpR1Binding = GP_BIND_R;
    gpBackBinding = GP_BIND_2;
    gpStartBinding = GP_BIND_4;
}

void writeGPConfigLine(File& file, const char* key, const char* value) {
    file.print(key);
    file.print('=');
    file.println(value);
}

void writeGPConfigLine(File& file, const char* key, const String& value) {
    file.print(key);
    file.print('=');
    file.println(value);
}

void applyGPConfigLine(const String& rawLine) {
    String line = rawLine;
    line.trim();
    if (!line.length()) return;

    const int separator = line.indexOf('=');
    if (separator < 0) return;

    String key = line.substring(0, separator);
    String value = line.substring(separator + 1);
    key.trim();
    value.trim();

    if (key.equalsIgnoreCase("profile")) {
        gpProfile = parseGPProfile(value);
    } else if (key.equalsIgnoreCase("move_input")) {
        gpMoveInput = parseGPMoveInput(value);
    } else if (key.equalsIgnoreCase("motion_sens")) {
        const float parsed = value.toFloat();
        if (parsed >= 0.5f && parsed <= 2.0f) gpMotionSensitivity = parsed;
    } else if (key.equalsIgnoreCase("invert_motion_x")) {
        gpInvertMotionX = parseGPBool(value, gpInvertMotionX);
    } else if (key.equalsIgnoreCase("invert_motion_y")) {
        gpInvertMotionY = parseGPBool(value, gpInvertMotionY);
    } else if (key.equalsIgnoreCase("brightness")) {
        gpBrightnessLevel = clampDisplayBrightnessLevel(value.toInt());
    } else if (key.equalsIgnoreCase("button_a")) {
        gpABinding = parseGPBinding(value);
    } else if (key.equalsIgnoreCase("button_b")) {
        gpBBinding = parseGPBinding(value);
    } else if (key.equalsIgnoreCase("button_x")) {
        gpXBinding = parseGPBinding(value);
    } else if (key.equalsIgnoreCase("button_y")) {
        gpYBinding = parseGPBinding(value);
    } else if (key.equalsIgnoreCase("button_l1")) {
        gpL1Binding = parseGPBinding(value);
    } else if (key.equalsIgnoreCase("button_r1")) {
        gpR1Binding = parseGPBinding(value);
    } else if (key.equalsIgnoreCase("button_back")) {
        gpBackBinding = parseGPBinding(value);
    } else if (key.equalsIgnoreCase("button_start")) {
        gpStartBinding = parseGPBinding(value);
    }
}

void loadGPSettings() {
    loadGPDefaults();

    if (!sdAvailable || !SD.exists(kGamepadConfigPath)) {
        syncGPDisplayBrightness();
        return;
    }

    File file = SD.open(kGamepadConfigPath, FILE_READ);
    if (!file) {
        syncGPDisplayBrightness();
        return;
    }

    while (file.available()) {
        applyGPConfigLine(file.readStringUntil('\n'));
    }
    file.close();

    syncGPDisplayBrightness();
}

void saveGPSettings() {
    if (!sdAvailable) return;

    if (SD.exists(kGamepadConfigPath)) SD.remove(kGamepadConfigPath);

    File file = SD.open(kGamepadConfigPath, FILE_WRITE);
    if (!file) return;

    writeGPConfigLine(file, "profile", getGPProfileConfigValue(gpProfile));
    writeGPConfigLine(file, "move_input", getGPMoveInputConfigValue(gpMoveInput));
    writeGPConfigLine(file, "motion_sens", String(gpMotionSensitivity, 1));
    writeGPConfigLine(file, "invert_motion_x", gpInvertMotionX ? "true" : "false");
    writeGPConfigLine(file, "invert_motion_y", gpInvertMotionY ? "true" : "false");
    writeGPConfigLine(file, "brightness", String(static_cast<int>(gpBrightnessLevel)));
    writeGPConfigLine(file, "button_a", getGPBindingLabel(gpABinding));
    writeGPConfigLine(file, "button_b", getGPBindingLabel(gpBBinding));
    writeGPConfigLine(file, "button_x", getGPBindingLabel(gpXBinding));
    writeGPConfigLine(file, "button_y", getGPBindingLabel(gpYBinding));
    writeGPConfigLine(file, "button_l1", getGPBindingLabel(gpL1Binding));
    writeGPConfigLine(file, "button_r1", getGPBindingLabel(gpR1Binding));
    writeGPConfigLine(file, "button_back", getGPBindingLabel(gpBackBinding));
    writeGPConfigLine(file, "button_start", getGPBindingLabel(gpStartBinding));
    file.close();
}

void clearGPExitArm() {
    gpExitArmed = false;
}

bool hasGPExitHint(unsigned long now) {
    return gpExitArmed && (now - gpExitArmMillis) <= GP_EXIT_DOUBLE_TAP_MS;
}

int16_t axisFromKeys(bool negative, bool positive) {
    if (negative == positive) return 0;
    return positive ? GP_AXIS_MAX : GP_AXIS_MIN;
}

signed char hatFromKeys(bool up, bool down, bool left, bool right) {
    if (up && right) return DPAD_UP_RIGHT;
    if (up && left) return DPAD_UP_LEFT;
    if (down && right) return DPAD_DOWN_RIGHT;
    if (down && left) return DPAD_DOWN_LEFT;
    if (up) return DPAD_UP;
    if (down) return DPAD_DOWN;
    if (left) return DPAD_LEFT;
    if (right) return DPAD_RIGHT;
    return DPAD_CENTERED;
}

String getStickSummary() {
    if (gpMoveInput == GP_MOVE_MOTION) {
        return gpProfile == GP_PROFILE_TWIN_STICK ? "Move Motion  Aim JKLO" : "Move Motion  Aim ASDE";
    }
    if (gpProfile == GP_PROFILE_TWIN_STICK) return "Move ASDE  Aim JKLO";
    return "Move JKLO  Aim ASDE";
}

void drawGPCalibrationOverlay(const char* label) {
    drawGPCard(52, 44, 136, 40, false);
    gpCanvas->setTextSize(2);
    gpCanvas->setTextColor(YELLOW);
    gpCanvas->setCursor(66, 57);
    gpCanvas->println(label);
}

void calibrateGPMotion() {
    drawGPBackground();
    drawGPHeader("BLE Gamepad", "Keep the Cardputer steady");
    drawGPCalibrationOverlay("CAL...");

    float sumPitch = 0.0f;
    float sumRoll = 0.0f;
    int validSamples = 0;
    const int samples = 80;

    for (int i = 0; i < samples; ++i) {
        M5Cardputer.update();

        float ax = 0.0f;
        float ay = 0.0f;
        float az = 0.0f;
        if (M5.Imu.getAccelData(&ax, &ay, &az)) {
            const float pitch = atan2(-ax, sqrt((ay * ay) + (az * az))) * 180.0f / M_PI;
            const float roll = atan2(ay, az) * 180.0f / M_PI;
            sumPitch += pitch;
            sumRoll += roll;
            ++validSamples;
        }
        delay(10);
    }

    gpImuReady = validSamples > 0;
    if (gpImuReady) {
        gpPitchOffset = sumPitch / validSamples;
        gpRollOffset = sumRoll / validSamples;
    } else {
        gpPitchOffset = 0.0f;
        gpRollOffset = 0.0f;
    }
}

void applyMotionMove(int16_t& moveX, int16_t& moveY) {
    if (gpMoveInput != GP_MOVE_MOTION || !gpImuReady) return;

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    if (!M5.Imu.getAccelData(&ax, &ay, &az)) return;

    const float pitch = (atan2(-ax, sqrt((ay * ay) + (az * az))) * 180.0f / M_PI) - gpPitchOffset;
    const float roll = (atan2(ay, az) * 180.0f / M_PI) - gpRollOffset;
    const float threshold = GP_MOTION_TILT_THRESHOLD_DEG / gpMotionSensitivity;

    moveX = 0;
    moveY = 0;

    // Cardputer tilt feels more natural with pitch controlling left/right
    // and roll controlling up/down.
    if (pitch <= -threshold) moveX = GP_AXIS_MIN;
    else if (pitch >= threshold) moveX = GP_AXIS_MAX;

    if (roll <= -threshold) moveY = GP_AXIS_MIN;
    else if (roll >= threshold) moveY = GP_AXIS_MAX;

    if (gpInvertMotionX) moveX = -moveX;
    if (gpInvertMotionY) moveY = -moveY;
}

const char* getGPStatusText() {
    if (!gpBleStarted) return "Starting BLE";
    return bleGamepad.isConnected() ? "Connected" : "Waiting pair";
}

String getBindingSummaryLine1() {
    String line = "A ";
    line += getGPBindingShortLabel(gpABinding);
    line += "  B ";
    line += getGPBindingShortLabel(gpBBinding);
    line += "  X ";
    line += getGPBindingShortLabel(gpXBinding);
    line += "  Y ";
    line += getGPBindingShortLabel(gpYBinding);
    return line;
}

String getBindingSummaryLine2() {
    String line = "L1 ";
    line += getGPBindingShortLabel(gpL1Binding);
    line += "  R1 ";
    line += getGPBindingShortLabel(gpR1Binding);
    line += "  Bk ";
    line += getGPBindingShortLabel(gpBackBinding);
    line += "  St ";
    line += getGPBindingShortLabel(gpStartBinding);
    return line;
}

String getHatLabel(signed char hat) {
    switch (hat) {
        case DPAD_UP:         return "Up";
        case DPAD_UP_RIGHT:   return "Up-Right";
        case DPAD_RIGHT:      return "Right";
        case DPAD_DOWN_RIGHT: return "Down-Right";
        case DPAD_DOWN:       return "Down";
        case DPAD_DOWN_LEFT:  return "Down-Left";
        case DPAD_LEFT:       return "Left";
        case DPAD_UP_LEFT:    return "Up-Left";
        default:              return "Center";
    }
}

String getActiveButtonsLabel(const GPReportState& report) {
    const char* labels[GP_BUTTON_COUNT] = {"A", "B", "X", "Y", "L1", "R1", "Bk", "St"};
    String text = "";
    for (int i = 0; i < GP_BUTTON_COUNT; ++i) {
        if (!report.buttons[i]) continue;
        if (text.length()) text += ' ';
        text += labels[i];
    }
    if (!text.length()) text = "None";
    return text;
}

String getLiveLine1(const GPReportState& report) {
    String line = "Hat ";
    line += getHatLabel(report.hat);
    line += "  Btn ";
    line += getActiveButtonsLabel(report);
    return line;
}

String getLiveLine2(const GPReportState& report) {
    String line = "LX ";
    line += String(report.leftX / GP_AXIS_MAX);
    line += "  LY ";
    line += String(report.leftY / GP_AXIS_MAX);
    line += "  RX ";
    line += String(report.rightX / GP_AXIS_MAX);
    line += "  RY ";
    line += String(report.rightY / GP_AXIS_MAX);
    return line;
}

bool gpReportsEqual(const GPReportState& left, const GPReportState& right) {
    if (left.leftX != right.leftX || left.leftY != right.leftY ||
        left.rightX != right.rightX || left.rightY != right.rightY ||
        left.hat != right.hat) {
        return false;
    }

    for (int i = 0; i < GP_BUTTON_COUNT; ++i) {
        if (left.buttons[i] != right.buttons[i]) return false;
    }
    return true;
}

void applyGPReport(const GPReportState& report) {
    if (!gpBleStarted) return;
    if (!bleGamepad.isConnected()) return;

    bleGamepad.setX(report.leftX);
    bleGamepad.setY(report.leftY);
    bleGamepad.setZ(report.rightX);
    bleGamepad.setRZ(report.rightY);
    bleGamepad.setHat1(report.hat);

    for (uint8_t i = 0; i < GP_BUTTON_COUNT; ++i) {
        const uint8_t button = BUTTON_1 + i;
        if (report.buttons[i]) bleGamepad.press(button);
        else bleGamepad.release(button);
    }

    bleGamepad.sendReport();
}

GPReportState buildNeutralGPReport() {
    GPReportState report;
    report.hat = DPAD_CENTERED;
    return report;
}

void sendNeutralGPReport() {
    gpLastReport = buildNeutralGPReport();
    gpHasLastReport = true;
    applyGPReport(gpLastReport);
}

GPReportState buildGPReport(const Keyboard_Class::KeysState& keyState) {
    GPReportState report;

    const bool asdeUp = isAlphaKeyPressed('e', 'E');
    const bool asdeDown = isAlphaKeyPressed('s', 'S');
    const bool asdeLeft = isAlphaKeyPressed('a', 'A');
    const bool asdeRight = isAlphaKeyPressed('d', 'D');

    const bool jkloUp = isAlphaKeyPressed('o', 'O');
    const bool jkloDown = isAlphaKeyPressed('k', 'K');
    const bool jkloLeft = isAlphaKeyPressed('j', 'J');
    const bool jkloRight = isAlphaKeyPressed('l', 'L');

    if (gpProfile == GP_PROFILE_TWIN_STICK) {
        report.leftX = axisFromKeys(asdeLeft, asdeRight);
        report.leftY = axisFromKeys(asdeUp, asdeDown);
        report.rightX = axisFromKeys(jkloLeft, jkloRight);
        report.rightY = axisFromKeys(jkloUp, jkloDown);
    } else {
        report.leftX = axisFromKeys(jkloLeft, jkloRight);
        report.leftY = axisFromKeys(jkloUp, jkloDown);
        report.rightX = axisFromKeys(asdeLeft, asdeRight);
        report.rightY = axisFromKeys(asdeUp, asdeDown);
    }

    applyMotionMove(report.leftX, report.leftY);

    report.hat = hatFromKeys(
        M5Cardputer.Keyboard.isKeyPressed(';'),
        M5Cardputer.Keyboard.isKeyPressed('.'),
        M5Cardputer.Keyboard.isKeyPressed(','),
        M5Cardputer.Keyboard.isKeyPressed('/'));

    report.buttons[0] = isGPBindingPressed(gpABinding, keyState) || M5.BtnA.isPressed();
    report.buttons[1] = isGPBindingPressed(gpBBinding, keyState);
    report.buttons[2] = isGPBindingPressed(gpXBinding, keyState);
    report.buttons[3] = isGPBindingPressed(gpYBinding, keyState);
    report.buttons[4] = isGPBindingPressed(gpL1Binding, keyState);
    report.buttons[5] = isGPBindingPressed(gpR1Binding, keyState);
    report.buttons[6] = isGPBindingPressed(gpBackBinding, keyState);
    report.buttons[7] = isGPBindingPressed(gpStartBinding, keyState);

    return report;
}

void drawGPMainUI() {
    drawGPBackground();
    drawGPHeader("BLE Gamepad", "Cardputer defaults");

    drawGPCard(10, 30, 105, 26, false);
    drawGPCardTitle(18, 35, "Status", false);
    drawGPCardValue(18, 43, String(getGPStatusText()), false);

    drawGPCard(125, 30, 105, 26, false);
    drawGPCardTitle(133, 35, "Profile", false);
    drawGPCardValue(133, 43, String(getGPProfileLabel(gpProfile)), false);

    drawGPCard(10, 60, 220, 24, false);
    drawGPCardTitle(18, 64, "Controls", false);
    drawGPCardValue(18, 72, getStickSummary(), false);

    drawGPCard(10, 88, 220, 28, false);
    drawGPCardTitle(18, 91, "Bindings", false);
    drawGPCardValue(18, 99, getBindingSummaryLine1(), false);
    drawGPCardValue(18, 107, getBindingSummaryLine2(), false);

    if (hasGPExitHint(millis())) {
        drawGPFooter("Del again to exit", "Win: joy.cpl", TFT_YELLOW);
    } else {
        drawGPFooter("M menu  H help", "BtnA=A", gpCanvas->color565(220, 245, 255));
    }
}

void drawGPHelp() {
    drawGPBackground();
    drawGPHeader("BLE Gamepad", "Help");

    drawGPCard(10, 34, 220, 82, false);
    gpCanvas->setTextSize(1);
    gpCanvas->setTextColor(WHITE);
    gpCanvas->setCursor(18, 40);
    gpCanvas->println("Windows: pair, then open joy.cpl");
    gpCanvas->setCursor(18, 51);
    gpCanvas->println(gpMoveInput == GP_MOVE_MOTION ? "Move: Motion  |  Aim: keys" : "Sticks: ASDE / JKLO");
    gpCanvas->setCursor(18, 62);
    gpCanvas->println("D-pad: ; , . /");
    gpCanvas->setCursor(18, 73);
    gpCanvas->println("Twin Stick = left move, right aim");
    gpCanvas->setCursor(18, 84);
    gpCanvas->println(String("Move Input: ") + getGPMoveInputLabel(gpMoveInput));
    gpCanvas->setCursor(18, 95);
    gpCanvas->println(getBindingSummaryLine1());
    gpCanvas->setCursor(18, 106);
    gpCanvas->println(getBindingSummaryLine2());

    if (hasGPExitHint(millis())) {
        drawGPFooter("Del again to exit", "H close", TFT_YELLOW);
    } else {
        drawGPFooter("BtnA=A  Del Del exit", "H close", gpCanvas->color565(220, 245, 255));
    }
}

void drawGPMenuRow(int y, const char* label, const String& value, bool selected) {
    drawGPCard(10, y - 6, 220, 14, selected);
    drawGPCardTitle(18, y - 2, label, selected);
    const int valueX = 224 - (value.length() * 6);
    drawGPCardValue(valueX, y - 2, value, selected);
}

void drawGPMenu() {
    drawGPBackground();
    drawGPHeader("BLE Gamepad", "Settings");

    const int visibleRows = 6;
    int start = gpMenuIndex - (visibleRows / 2);
    if (start < 0) start = 0;
    if (start > GP_MENU_COUNT - visibleRows) start = GP_MENU_COUNT - visibleRows;
    if (start < 0) start = 0;

    const char* labels[GP_MENU_COUNT] = {
        "Profile",
        "Move Input",
        "Motion Sens",
        "Invert X",
        "Invert Y",
        "Brightness",
        "A Button",
        "B Button",
        "X Button",
        "Y Button",
        "L1 Button",
        "R1 Button",
        "Back Button",
        "Start Button",
        "Save & Exit"
    };

    String values[GP_MENU_COUNT] = {
        String(getGPProfileLabel(gpProfile)),
        String(getGPMoveInputLabel(gpMoveInput)),
        String(gpMotionSensitivity, 1),
        String(getGPOnOffLabel(gpInvertMotionX)),
        String(getGPOnOffLabel(gpInvertMotionY)),
        String(static_cast<int>(gpBrightnessLevel)),
        String(getGPBindingLabel(gpABinding)),
        String(getGPBindingLabel(gpBBinding)),
        String(getGPBindingLabel(gpXBinding)),
        String(getGPBindingLabel(gpYBinding)),
        String(getGPBindingLabel(gpL1Binding)),
        String(getGPBindingLabel(gpR1Binding)),
        String(getGPBindingLabel(gpBackBinding)),
        String(getGPBindingLabel(gpStartBinding)),
        ""
    };

    int y = 46;
    for (int i = start; i < GP_MENU_COUNT && i < start + visibleRows; ++i) {
        drawGPMenuRow(y, labels[i], values[i], i == gpMenuIndex);
        y += 13;
    }

    if (hasGPExitHint(millis())) {
        drawGPFooter("Del again to exit", "Ent save  ;/. nav", TFT_YELLOW);
    } else {
        drawGPFooter("Ent save  ESC close", ",/ edit", gpCanvas->color565(220, 245, 255));
    }
}

void refreshGPUI() {
    if (gpCanvas == nullptr) return;
    if (gpInMenu) drawGPMenu();
    else if (gpShowHelp) drawGPHelp();
    else drawGPMainUI();
    gpCanvas->pushSprite(0, 0);
}

void updateGPMenuSetting(int direction) {
    switch (gpMenuIndex) {
        case GP_MENU_PROFILE:
            gpProfile = cycleGPProfile(gpProfile, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_MOVE_INPUT:
            gpMoveInput = cycleGPMoveInput(gpMoveInput, direction);
            if (gpMoveInput == GP_MOVE_MOTION) calibrateGPMotion();
            gpSettingsChanged = true;
            break;
        case GP_MENU_MOTION_SENS:
            gpMotionSensitivity = constrain(gpMotionSensitivity + (0.1f * direction), 0.5f, 2.0f);
            gpSettingsChanged = true;
            break;
        case GP_MENU_INVERT_X:
            gpInvertMotionX = !gpInvertMotionX;
            gpSettingsChanged = true;
            break;
        case GP_MENU_INVERT_Y:
            gpInvertMotionY = !gpInvertMotionY;
            gpSettingsChanged = true;
            break;
        case GP_MENU_BRIGHTNESS:
            gpBrightnessLevel = clampDisplayBrightnessLevel(static_cast<int>(gpBrightnessLevel) + direction);
            gpSettingsChanged = true;
            syncGPDisplayBrightness();
            break;
        case GP_MENU_A_BUTTON:
            gpABinding = cycleGPBinding(gpABinding, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_B_BUTTON:
            gpBBinding = cycleGPBinding(gpBBinding, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_X_BUTTON:
            gpXBinding = cycleGPBinding(gpXBinding, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_Y_BUTTON:
            gpYBinding = cycleGPBinding(gpYBinding, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_L1_BUTTON:
            gpL1Binding = cycleGPBinding(gpL1Binding, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_R1_BUTTON:
            gpR1Binding = cycleGPBinding(gpR1Binding, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_BACK_BUTTON:
            gpBackBinding = cycleGPBinding(gpBackBinding, direction);
            gpSettingsChanged = true;
            break;
        case GP_MENU_START_BUTTON:
            gpStartBinding = cycleGPBinding(gpStartBinding, direction);
            gpSettingsChanged = true;
            break;
        default:
            break;
    }
}

void closeGPMenu() {
    if (gpSettingsChanged) {
        saveGPSettings();
        gpSettingsChanged = false;
    }
    gpInMenu = false;
    clearGPExitArm();
    refreshGPUI();
}

bool handleGPBrightnessShortcut(const Keyboard_Class::KeysState& keyState) {
    const bool comboPressed = keyState.ctrl && keyState.del;
    const bool comboEdge = comboPressed && !gpBrightnessShortcutPressed;
    gpBrightnessShortcutPressed = comboPressed;

    if (!comboPressed) return false;

    gpDelWasPressed = keyState.del;
    clearGPExitArm();

    if (comboEdge) {
        gpBrightnessMuted = !gpBrightnessMuted;
        syncGPDisplayBrightness();
        sendNeutralGPReport();
        refreshGPUI();
    }

    return true;
}

bool handleGPExitGesture(const Keyboard_Class::KeysState& keyState, unsigned long now) {
    const bool delPressed = keyState.del;
    const bool delEdge = delPressed && !gpDelWasPressed;
    gpDelWasPressed = delPressed;

    if (!delEdge) return false;

    if (gpExitArmed && (now - gpExitArmMillis) <= GP_EXIT_DOUBLE_TAP_MS) {
        clearGPExitArm();
        gpBrightnessMuted = false;
        syncGPDisplayBrightness();
        sendNeutralGPReport();
        returnToMenu = true;
        return true;
    }

    gpExitArmed = true;
    gpExitArmMillis = now;
    refreshGPUI();
    return false;
}

void flushGPExitHint(unsigned long now) {
    if (!gpExitArmed) return;
    if ((now - gpExitArmMillis) <= GP_EXIT_DOUBLE_TAP_MS) return;
    clearGPExitArm();
    refreshGPUI();
}

void handleGPSettingsMenu(unsigned long now) {
    if (now - gpLastKeyPress <= 180) return;

    bool redraw = false;

    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        gpMenuIndex = (gpMenuIndex - 1 + GP_MENU_COUNT) % GP_MENU_COUNT;
        redraw = true;
        gpLastKeyPress = now;
    } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        gpMenuIndex = (gpMenuIndex + 1) % GP_MENU_COUNT;
        redraw = true;
        gpLastKeyPress = now;
    } else if (M5Cardputer.Keyboard.isKeyPressed(',')) {
        updateGPMenuSetting(-1);
        redraw = true;
        gpLastKeyPress = now;
    } else if (M5Cardputer.Keyboard.isKeyPressed('/')) {
        updateGPMenuSetting(1);
        redraw = true;
        gpLastKeyPress = now;
    } else if (M5Cardputer.Keyboard.isKeyPressed('`')) {
        closeGPMenu();
        gpLastKeyPress = now;
    } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) || M5.BtnA.isPressed()) {
        if (gpMenuIndex == GP_MENU_EXIT) {
            closeGPMenu();
        } else {
            updateGPMenuSetting(1);
            redraw = true;
        }
        gpLastKeyPress = now;
    }

    if (redraw && gpInMenu) refreshGPUI();
}

void syncGPReport(const GPReportState& report) {
    const bool changed = !gpHasLastReport || !gpReportsEqual(report, gpLastReport);
    if (!changed) return;

    gpLastReport = report;
    gpHasLastReport = true;
    applyGPReport(report);
    refreshGPUI();
}

void startGamepadBle() {
    if (gpBleStarted) return;

    Serial.println("[gamepad] starting BLE stack");

    BleGamepadConfiguration config;
    config.setAutoReport(false);
    config.setControllerType(CONTROLLER_TYPE_GAMEPAD);
    config.setButtonCount(GP_BUTTON_COUNT);
    config.setHatSwitchCount(1);
    config.setAxesMin(GP_AXIS_MIN);
    config.setAxesMax(GP_AXIS_MAX);
    config.setWhichAxes(true, true, true, false, false, true, false, false);
    bleGamepad.begin(&config);

    gpBleStarted = true;
    gpWasConnected = false;
    refreshGPUI();
}

}  // namespace

void gamepadInit() {
    Serial.println("[gamepad] mode selected");
    if (gpCanvas == nullptr) {
        gpCanvas = new M5Canvas(&M5Cardputer.Display);
        gpCanvas->createSprite(240, 135);
        gpCanvas->setTextWrap(false);
    }
    M5.Imu.begin();
    gpImuReady = false;
    gpBleStarted = false;
    gpModeStartMillis = millis();
}

void gamepadResetUI() {
    Serial.println("[gamepad] reset UI");
    loadGPSettings();
    gpSettingsChanged = false;
    gpInMenu = false;
    gpShowHelp = false;
    gpMenuIndex = 0;
    gpLastKeyPress = 0;
    gpDelWasPressed = false;
    gpBrightnessShortcutPressed = false;
    clearGPExitArm();
    gpModeStartMillis = millis();
    gpWasConnected = false;
    gpLastReport = buildNeutralGPReport();
    gpHasLastReport = true;
    if (gpMoveInput == GP_MOVE_MOTION) calibrateGPMotion();
    refreshGPUI();
}

void gamepadLoop() {
    const Keyboard_Class::KeysState& keyState = M5Cardputer.Keyboard.keysState();
    const unsigned long now = millis();
    if (!gpBleStarted && (now - gpModeStartMillis) >= GP_BLE_START_DELAY_MS) {
        startGamepadBle();
    }

    const bool connected = gpBleStarted && bleGamepad.isConnected();

    if (connected != gpWasConnected) {
        gpWasConnected = connected;
        Serial.printf("[gamepad] connected=%d\n", connected ? 1 : 0);
        if (connected && gpHasLastReport) applyGPReport(gpLastReport);
        refreshGPUI();
    }

    flushGPExitHint(now);

    if (handleGPBrightnessShortcut(keyState)) return;

    if (handleGPExitGesture(keyState, now)) return;

    if (gpInMenu) {
        handleGPSettingsMenu(now);
        return;
    }

    if ((M5Cardputer.Keyboard.isKeyPressed('m') || M5Cardputer.Keyboard.isKeyPressed('M')) &&
        (now - gpLastKeyPress > 300)) {
        gpInMenu = true;
        gpShowHelp = false;
        sendNeutralGPReport();
        refreshGPUI();
        gpLastKeyPress = now;
        return;
    }

    if ((M5Cardputer.Keyboard.isKeyPressed('h') || M5Cardputer.Keyboard.isKeyPressed('H')) &&
        (now - gpLastKeyPress > 300)) {
        gpShowHelp = !gpShowHelp;
        sendNeutralGPReport();
        refreshGPUI();
        gpLastKeyPress = now;
        return;
    }

    if (gpShowHelp) return;

    syncGPReport(buildGPReport(keyState));
}

bool gamepadBlocksExit() {
    return false;
}
