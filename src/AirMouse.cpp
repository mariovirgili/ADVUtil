#include "AirMouse.h"

#include "BleComboHID.h"

#include <M5Cardputer.h>
#include <SD.h>
#include <vector>

// Import the shared SD/menu state from main.cpp.
extern bool sdAvailable;
extern bool returnToMenu;

BleComboHID bleCombo("Cardputer Mouse", "M5Stack", 100);

const char* amConfigPath = "/ADVUtil/airmouse.cfg";
const char* amMacroConfigPath = "/ADVUtil/airmouse_macros.cfg";
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

enum AMMacroView : uint8_t {
    AM_MACRO_VIEW_HOME = 0,
    AM_MACRO_VIEW_RECORD_SELECT,
    AM_MACRO_VIEW_RECORDING,
    AM_MACRO_VIEW_LIST,
    AM_MACRO_VIEW_PLAYBACK
};

struct AMMacroStep {
    uint8_t modifiers = 0;
    uint8_t keys[6] = {0, 0, 0, 0, 0, 0};
};

struct AMMacroSlot {
    String preview = "";
    std::vector<AMMacroStep> steps;
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
bool amMacroMode = false;
bool amBtnALongHandled = false;
int amMacroListScroll = 0;
int amMacroSelectedSlot = -1;
int amMacroPlaybackSlot = -1;
size_t amMacroPlaybackIndex = 0;
unsigned long amMacroPlaybackMillis = 0;
unsigned long amMacroStatusUntil = 0;
String amMacroStatusMessage = "";
String amMacroRecordingPreview = "";
bool amMacroRecordingTruncated = false;
AMMacroView amMacroView = AM_MACRO_VIEW_HOME;
AMMacroStep amMacroLastInputStep;
bool amMacroHasLastInputStep = false;
std::vector<AMMacroStep> amMacroRecordingSteps;
AMMacroSlot amMacroSlots[10];

constexpr unsigned long AM_EXIT_DOUBLE_TAP_MS = 550;
constexpr unsigned long AM_MACRO_HOLD_MS = 2000;
constexpr unsigned long AM_MACRO_STATUS_MS = 2000;
constexpr unsigned long AM_MACRO_PLAYBACK_STEP_MS = 70;
constexpr size_t AM_MACRO_SLOT_COUNT = 10;
constexpr size_t AM_MACRO_MAX_STEPS = 120;
constexpr uint8_t AM_HID_RIGHT_ARROW = 0x4F;
constexpr uint8_t AM_HID_LEFT_ARROW = 0x50;
constexpr uint8_t AM_HID_DOWN_ARROW = 0x51;
constexpr uint8_t AM_HID_UP_ARROW = 0x52;

void clearExitArm();

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

void writeConfigLine(File& file, const String& key, const String& value) {
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

const char* getMacroSlotLabel(int slot) {
    static const char* labels[AM_MACRO_SLOT_COUNT] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
    if (slot < 0 || slot >= static_cast<int>(AM_MACRO_SLOT_COUNT)) return "?";
    return labels[slot];
}

int getMacroSlotIndex(char key) {
    if (key >= '1' && key <= '9') return key - '1';
    if (key == '0') return 9;
    return -1;
}

bool isMacroSlotUsed(int slot) {
    return slot >= 0
        && slot < static_cast<int>(AM_MACRO_SLOT_COUNT)
        && !amMacroSlots[slot].steps.empty();
}

int getUsedMacroCount() {
    int used = 0;
    for (size_t i = 0; i < AM_MACRO_SLOT_COUNT; ++i) {
        if (isMacroSlotUsed(static_cast<int>(i))) ++used;
    }
    return used;
}

void clearMacroSlot(int slot) {
    if (slot < 0 || slot >= static_cast<int>(AM_MACRO_SLOT_COUNT)) return;
    amMacroSlots[slot].preview = "";
    amMacroSlots[slot].steps.clear();
}

char toHexDigit(uint8_t value) {
    return value < 10 ? static_cast<char>('0' + value) : static_cast<char>('A' + (value - 10));
}

int fromHexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

String encodeHexText(const String& text) {
    String encoded = "";
    encoded.reserve(text.length() * 2);
    for (size_t i = 0; i < text.length(); ++i) {
        const uint8_t value = static_cast<uint8_t>(text.charAt(i));
        encoded += toHexDigit((value >> 4) & 0x0F);
        encoded += toHexDigit(value & 0x0F);
    }
    return encoded;
}

bool decodeHexByte(const String& text, int offset, uint8_t& value) {
    if (offset < 0 || (offset + 1) >= text.length()) return false;
    const int high = fromHexDigit(text.charAt(offset));
    const int low = fromHexDigit(text.charAt(offset + 1));
    if (high < 0 || low < 0) return false;
    value = static_cast<uint8_t>((high << 4) | low);
    return true;
}

String decodeHexText(const String& text) {
    String decoded = "";
    if ((text.length() % 2) != 0) return decoded;

    decoded.reserve(text.length() / 2);
    for (int i = 0; i < text.length(); i += 2) {
        uint8_t value = 0;
        if (!decodeHexByte(text, i, value)) {
            decoded = "";
            break;
        }
        decoded += static_cast<char>(value);
    }
    return decoded;
}

String encodeMacroSteps(const std::vector<AMMacroStep>& steps) {
    String encoded = "";
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) encoded += ',';
        encoded += toHexDigit((steps[i].modifiers >> 4) & 0x0F);
        encoded += toHexDigit(steps[i].modifiers & 0x0F);
        for (size_t key = 0; key < 6; ++key) {
            encoded += toHexDigit((steps[i].keys[key] >> 4) & 0x0F);
            encoded += toHexDigit(steps[i].keys[key] & 0x0F);
        }
    }
    return encoded;
}

bool decodeMacroSteps(const String& text, std::vector<AMMacroStep>& steps) {
    steps.clear();
    if (text.length() == 0) return true;

    int start = 0;
    while (start < text.length()) {
        int end = text.indexOf(',', start);
        if (end < 0) end = text.length();

        String chunk = text.substring(start, end);
        chunk.trim();
        if (chunk.length() != 14) return false;

        AMMacroStep step;
        if (!decodeHexByte(chunk, 0, step.modifiers)) return false;
        for (int i = 0; i < 6; ++i) {
            if (!decodeHexByte(chunk, 2 + (i * 2), step.keys[i])) return false;
        }
        steps.push_back(step);
        start = end + 1;
    }

    return true;
}

void loadAMMacros() {
    for (size_t i = 0; i < AM_MACRO_SLOT_COUNT; ++i) {
        clearMacroSlot(static_cast<int>(i));
    }

    if (!sdAvailable || !SD.exists(amMacroConfigPath)) return;

    File file = SD.open(amMacroConfigPath, FILE_READ);
    if (!file) return;

    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (!line.length()) continue;

        const int separator = line.indexOf('=');
        if (separator < 0) continue;

        String key = line.substring(0, separator);
        String value = line.substring(separator + 1);
        key.trim();
        value.trim();

        if (!key.startsWith("macro_")) continue;

        const int secondSeparator = key.indexOf('_', 6);
        if (secondSeparator < 0) continue;

        const int slot = key.substring(6, secondSeparator).toInt();
        if (slot < 0 || slot >= static_cast<int>(AM_MACRO_SLOT_COUNT)) continue;

        const String field = key.substring(secondSeparator + 1);
        if (field.equalsIgnoreCase("text")) {
            amMacroSlots[slot].preview = decodeHexText(value);
        } else if (field.equalsIgnoreCase("steps")) {
            decodeMacroSteps(value, amMacroSlots[slot].steps);
        }
    }

    file.close();
}

void saveAMMacros() {
    if (!sdAvailable) return;

    if (SD.exists(amMacroConfigPath)) SD.remove(amMacroConfigPath);

    File file = SD.open(amMacroConfigPath, FILE_WRITE);
    if (!file) return;

    for (size_t i = 0; i < AM_MACRO_SLOT_COUNT; ++i) {
        if (amMacroSlots[i].preview.length() == 0 && amMacroSlots[i].steps.empty()) continue;

        const String prefix = "macro_" + String(static_cast<int>(i)) + "_";
        writeConfigLine(file, prefix + "text", encodeHexText(amMacroSlots[i].preview));
        writeConfigLine(file, prefix + "steps", encodeMacroSteps(amMacroSlots[i].steps));
    }

    file.close();
}

void setMacroStatus(const String& message, unsigned long now, unsigned long duration = AM_MACRO_STATUS_MS) {
    amMacroStatusMessage = message;
    amMacroStatusUntil = now + duration;
}

bool hasMacroStatus(unsigned long now) {
    return amMacroStatusMessage.length() > 0 && now < amMacroStatusUntil;
}

String getMacroSlotPreview(int slot) {
    if (!isMacroSlotUsed(slot)) return "[empty]";
    return amMacroSlots[slot].preview.length() ? amMacroSlots[slot].preview : "[saved]";
}

void wrapTextLines(const String& text, int maxChars, std::vector<String>& lines) {
    lines.clear();
    if (maxChars <= 0) {
        lines.push_back(text);
        return;
    }

    int start = 0;
    while (start < text.length()) {
        while (start < text.length() && text.charAt(start) == ' ') ++start;
        if (start >= text.length()) break;

        int hardEnd = start + maxChars;
        if (hardEnd >= text.length()) {
            lines.push_back(text.substring(start));
            break;
        }

        int split = hardEnd;
        while (split > start && text.charAt(split) != ' ') --split;
        if (split == start) split = hardEnd;

        String line = text.substring(start, split);
        line.trim();
        if (!line.length()) line = text.substring(start, hardEnd);
        lines.push_back(line);
        start = split;
    }

    if (lines.empty()) lines.push_back("");
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

bool macroStepEquals(const AMMacroStep& left, const AMMacroStep& right) {
    if (left.modifiers != right.modifiers) return false;
    for (int i = 0; i < 6; ++i) {
        if (left.keys[i] != right.keys[i]) return false;
    }
    return true;
}

bool isMacroStepEmpty(const AMMacroStep& step) {
    if (step.modifiers != 0) return false;
    for (int i = 0; i < 6; ++i) {
        if (step.keys[i] != 0) return false;
    }
    return true;
}

size_t getMacroStepKeyCount(const AMMacroStep& step) {
    size_t count = 0;
    for (int i = 0; i < 6; ++i) {
        if (step.keys[i] != 0) count = static_cast<size_t>(i + 1);
    }
    return count;
}

AMMacroStep buildMacroStep(const Keyboard_Class::KeysState& keyState) {
    AMMacroStep step;
    step.modifiers = keyState.modifiers;

    size_t keyCount = 0;
    for (uint8_t hidKey : keyState.hid_keys) {
        if (keyCount < 6) step.keys[keyCount++] = remapFnArrowKey(keyState, hidKey);
    }

    return step;
}

void clearMacroInputTracking() {
    amMacroHasLastInputStep = false;
}

void seedMacroInputTracking(const Keyboard_Class::KeysState& keyState) {
    amMacroLastInputStep = buildMacroStep(keyState);
    amMacroHasLastInputStep = true;
}

bool macroCharPressed(const Keyboard_Class::KeysState& keyState, char lower, char upper) {
    for (char c : keyState.word) {
        if (c == lower || c == upper) return true;
    }
    return false;
}

bool macroBacktickPressed(const Keyboard_Class::KeysState& keyState) {
    for (char c : keyState.word) {
        if (c == '`') return true;
    }
    return false;
}

int getPressedMacroSlot(const Keyboard_Class::KeysState& keyState) {
    for (char c : keyState.word) {
        const int slot = getMacroSlotIndex(c);
        if (slot >= 0) return slot;
    }
    return -1;
}

void appendMacroPreviewToken(const String& token) {
    if (!token.length() || token == "Ready to type") return;
    if (amMacroRecordingPreview.length() > 0) amMacroRecordingPreview += " | ";
    amMacroRecordingPreview += token;
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

void drawWrappedTextBlock(const String& text, int x, int y, int maxChars, int maxLines, uint16_t color) {
    std::vector<String> lines;
    wrapTextLines(text, maxChars, lines);

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(color);
    for (int i = 0; i < maxLines && i < static_cast<int>(lines.size()); ++i) {
        M5.Display.setCursor(x, y + (i * 10));
        M5.Display.println(lines[i]);
    }
}

void buildMacroListLines(std::vector<String>& lines) {
    lines.clear();

    for (int slot = 0; slot < static_cast<int>(AM_MACRO_SLOT_COUNT); ++slot) {
        std::vector<String> wrapped;
        wrapTextLines(getMacroSlotPreview(slot), 31, wrapped);
        if (wrapped.empty()) wrapped.push_back("[empty]");

        lines.push_back(String(getMacroSlotLabel(slot)) + ": " + wrapped[0]);
        for (size_t i = 1; i < wrapped.size(); ++i) {
            lines.push_back(String("   ") + wrapped[i]);
        }
    }
}

void drawAMMacroHome() {
    drawAMBackground();
    drawAMHeader("Macro Mode", amControlMode == AM_MODE_MOUSE ? "Overlay on Mouse mode" : "Overlay on Keyboard mode");

    drawAMCard(10, 34, 104, 28, false);
    drawAMCardTitle(20, 41, "Source", false);
    drawAMCardValue(20, 51, getModeText(), false);

    drawAMCard(126, 34, 104, 28, false);
    drawAMCardTitle(136, 41, "Saved", false);
    drawAMCardValue(136, 51, String(getUsedMacroCount()) + "/10", false);

    drawAMCard(10, 68, 220, 50, false);
    drawAMCardTitle(20, 75, "Commands", false);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(20, 86);
    M5.Display.println("1..0 play macro");
    M5.Display.setCursor(20, 96);
    M5.Display.println("R record/replace   L list macros");
    M5.Display.setCursor(20, 106);
    M5.Display.println("Hold BtnA to return");

    const bool showStatus = hasMacroStatus(millis());
    if (showStatus) {
        drawAMFooter(amMacroStatusMessage, "", TFT_YELLOW);
    }
}

void drawAMMacroRecordSelect() {
    drawAMBackground();
    drawAMHeader("Record Macro", "Choose a slot from 1 to 0");

    drawAMCard(10, 34, 220, 62, false);
    drawAMCardTitle(20, 41, "How to", false);
    drawWrappedTextBlock("Press 1..0 to overwrite or create the corresponding macro slot. Press R or ` to cancel.",
                         20, 54, 32, 4, WHITE);

    drawAMCard(10, 102, 220, 16, false);
    drawAMCardTitle(20, 107, "Filled", false);
    drawAMCardValue(62, 107, String(getUsedMacroCount()) + " saved macros", false);
}

void drawAMMacroRecording() {
    const String title = String("Recording Slot ") + getMacroSlotLabel(amMacroSelectedSlot);
    drawAMBackground();
    drawAMHeader(title.c_str(), "Press ` to stop and save");

    drawAMCard(10, 34, 220, 76, false);
    drawAMCardTitle(20, 41, "Captured", false);

    const String content = amMacroRecordingPreview.length() ? amMacroRecordingPreview : String("Press keys to start recording");
    drawWrappedTextBlock(content, 20, 54, 32, 5, WHITE);

    M5.Display.setTextColor(M5.Display.color565(220, 245, 255));
    M5.Display.setCursor(20, 104);
    M5.Display.printf("Steps: %d", static_cast<int>(amMacroRecordingSteps.size()));

    drawAMFooter("Hold BtnA exits", "` save macro", TFT_YELLOW);
}

void drawAMMacroList() {
    drawAMBackground();
    drawAMHeader("Macro List", "Use ; and . to scroll");

    drawAMCard(10, 34, 220, 76, false);

    std::vector<String> lines;
    buildMacroListLines(lines);

    const int visibleLines = 7;
    const int maxScroll = lines.size() > visibleLines ? static_cast<int>(lines.size()) - visibleLines : 0;
    if (amMacroListScroll > maxScroll) amMacroListScroll = maxScroll;
    if (amMacroListScroll < 0) amMacroListScroll = 0;

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(WHITE);
    int y = 42;
    for (int i = 0; i < visibleLines && (amMacroListScroll + i) < static_cast<int>(lines.size()); ++i) {
        M5.Display.setCursor(18, y);
        M5.Display.println(lines[amMacroListScroll + i]);
        y += 10;
    }

    if (maxScroll > 0) {
        M5.Display.setTextColor(M5.Display.color565(220, 245, 255));
        M5.Display.setCursor(210, 38);
        M5.Display.print("^");
        M5.Display.setCursor(210, 100);
        M5.Display.print("v");
    }

    drawAMFooter(";/. scroll", "L or ` close", M5.Display.color565(220, 245, 255));
}

void drawAMMacroPlayback() {
    const String title = String("Playing Slot ") + getMacroSlotLabel(amMacroPlaybackSlot);
    drawAMBackground();
    drawAMHeader(title.c_str(), "Sending the saved key sequence");

    drawAMCard(10, 40, 220, 58, false);
    drawAMCardTitle(20, 47, "Content", false);
    drawWrappedTextBlock(getMacroSlotPreview(amMacroPlaybackSlot), 20, 60, 32, 4, WHITE);

    drawAMFooter("Playback running", "Hold BtnA exit", TFT_YELLOW);
}

void drawAMMacroUI() {
    switch (amMacroView) {
        case AM_MACRO_VIEW_RECORD_SELECT:
            drawAMMacroRecordSelect();
            break;
        case AM_MACRO_VIEW_RECORDING:
            drawAMMacroRecording();
            break;
        case AM_MACRO_VIEW_LIST:
            drawAMMacroList();
            break;
        case AM_MACRO_VIEW_PLAYBACK:
            drawAMMacroPlayback();
            break;
        case AM_MACRO_VIEW_HOME:
        default:
            drawAMMacroHome();
            break;
    }
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
    if (amMacroMode) drawAMMacroUI();
    else if (amInMenu) drawAMMenu();
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

void resetMacroRecordingBuffers() {
    amMacroSelectedSlot = -1;
    amMacroRecordingSteps.clear();
    amMacroRecordingPreview = "";
    amMacroRecordingTruncated = false;
}

void enterMacroMode(const Keyboard_Class::KeysState& keyState, unsigned long now) {
    clearExitArm();
    releaseAllAMButtons();
    amShowMouseHelp = false;
    amMacroMode = true;
    amMacroView = AM_MACRO_VIEW_HOME;
    amMacroListScroll = 0;
    amMacroPlaybackSlot = -1;
    amMacroPlaybackIndex = 0;
    amMacroPlaybackMillis = 0;
    resetMacroRecordingBuffers();
    setMacroStatus("Macro mode ready", now, 1400);
    seedMacroInputTracking(keyState);
    refreshAMUI();
}

void exitMacroMode(const Keyboard_Class::KeysState& keyState) {
    releaseAllAMButtons();
    clearExitArm();
    amMacroMode = false;
    amMacroView = AM_MACRO_VIEW_HOME;
    amMacroListScroll = 0;
    amMacroPlaybackSlot = -1;
    amMacroPlaybackIndex = 0;
    amMacroPlaybackMillis = 0;
    amMacroStatusMessage = "";
    amMacroStatusUntil = 0;
    resetMacroRecordingBuffers();
    clearMacroInputTracking();
    refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
}

void openMacroHome(const Keyboard_Class::KeysState& keyState) {
    amMacroView = AM_MACRO_VIEW_HOME;
    amMacroListScroll = 0;
    resetMacroRecordingBuffers();
    amMacroPlaybackSlot = -1;
    amMacroPlaybackIndex = 0;
    amMacroPlaybackMillis = 0;
    seedMacroInputTracking(keyState);
    refreshAMUI();
}

void beginMacroRecordingSelection(const Keyboard_Class::KeysState& keyState) {
    amMacroView = AM_MACRO_VIEW_RECORD_SELECT;
    resetMacroRecordingBuffers();
    seedMacroInputTracking(keyState);
    refreshAMUI();
}

void beginMacroRecording(int slot, const Keyboard_Class::KeysState& keyState) {
    amMacroSelectedSlot = slot;
    amMacroRecordingSteps.clear();
    amMacroRecordingPreview = "";
    amMacroRecordingTruncated = false;
    amMacroView = AM_MACRO_VIEW_RECORDING;
    seedMacroInputTracking(keyState);
    refreshAMUI();
}

void finishMacroRecording(unsigned long now) {
    if (amMacroSelectedSlot < 0 || amMacroSelectedSlot >= static_cast<int>(AM_MACRO_SLOT_COUNT)) {
        openMacroHome(M5Cardputer.Keyboard.keysState());
        return;
    }

    if (!amMacroRecordingSteps.empty()
        && !isMacroStepEmpty(amMacroRecordingSteps.back())
        && amMacroRecordingSteps.size() < AM_MACRO_MAX_STEPS) {
        amMacroRecordingSteps.push_back(AMMacroStep());
    }

    amMacroSlots[amMacroSelectedSlot].steps = amMacroRecordingSteps;
    amMacroSlots[amMacroSelectedSlot].preview = amMacroRecordingSteps.empty() ? "" : amMacroRecordingPreview;
    saveAMMacros();

    String message = "Slot ";
    message += getMacroSlotLabel(amMacroSelectedSlot);
    if (amMacroRecordingSteps.empty()) {
        message += " cleared";
    } else if (amMacroRecordingTruncated) {
        message += " saved (max)";
    } else {
        message += " saved";
    }

    resetMacroRecordingBuffers();
    amMacroView = AM_MACRO_VIEW_HOME;
    setMacroStatus(message, now, 2200);
    seedMacroInputTracking(M5Cardputer.Keyboard.keysState());
    refreshAMUI();
}

void openMacroList(const Keyboard_Class::KeysState& keyState) {
    amMacroView = AM_MACRO_VIEW_LIST;
    amMacroListScroll = 0;
    seedMacroInputTracking(keyState);
    refreshAMUI();
}

void startMacroPlayback(int slot, unsigned long now) {
    if (!bleCombo.isConnected()) {
        setMacroStatus("BLE keyboard not connected", now);
        refreshAMUI();
        return;
    }

    if (!isMacroSlotUsed(slot)) {
        String message = "Slot ";
        message += getMacroSlotLabel(slot);
        message += " empty";
        setMacroStatus(message, now);
        refreshAMUI();
        return;
    }

    releaseAllAMButtons();
    amMacroPlaybackSlot = slot;
    amMacroPlaybackIndex = 0;
    amMacroPlaybackMillis = 0;
    amMacroView = AM_MACRO_VIEW_PLAYBACK;
    clearMacroInputTracking();
    refreshAMUI();
}

void stopMacroPlayback(unsigned long now, bool canceled = false) {
    const int slot = amMacroPlaybackSlot;

    bleCombo.releaseKeyboard();
    amMacroPlaybackSlot = -1;
    amMacroPlaybackIndex = 0;
    amMacroPlaybackMillis = 0;
    amMacroView = AM_MACRO_VIEW_HOME;

    if (slot >= 0) {
        String message = canceled ? "Playback canceled" : String("Slot ") + getMacroSlotLabel(slot) + " played";
        setMacroStatus(message, now, 1800);
    }

    seedMacroInputTracking(M5Cardputer.Keyboard.keysState());
    refreshAMUI();
}

void updateMacroPlayback(unsigned long now) {
    if (amMacroView != AM_MACRO_VIEW_PLAYBACK || amMacroPlaybackSlot < 0) return;

    const std::vector<AMMacroStep>& steps = amMacroSlots[amMacroPlaybackSlot].steps;
    if (amMacroPlaybackIndex >= steps.size()) {
        stopMacroPlayback(now);
        return;
    }

    if (amMacroPlaybackMillis != 0 && (now - amMacroPlaybackMillis) < AM_MACRO_PLAYBACK_STEP_MS) return;

    const AMMacroStep& step = steps[amMacroPlaybackIndex++];
    bleCombo.sendKeyboardReport(step.modifiers, step.keys, getMacroStepKeyCount(step));
    amMacroPlaybackMillis = now;
}

void refreshExpiredMacroStatus(const Keyboard_Class::KeysState& keyState, unsigned long now) {
    if (!amMacroStatusMessage.length() || now < amMacroStatusUntil) return;

    amMacroStatusMessage = "";
    amMacroStatusUntil = 0;
    if (amMacroMode && amMacroView == AM_MACRO_VIEW_HOME) {
        refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
    }
}

void handleMacroMode(const Keyboard_Class::KeysState& keyState, unsigned long now) {
    if (amMacroView == AM_MACRO_VIEW_PLAYBACK) {
        updateMacroPlayback(now);
        return;
    }

    const AMMacroStep currentStep = buildMacroStep(keyState);
    const bool changed = !amMacroHasLastInputStep || !macroStepEquals(currentStep, amMacroLastInputStep);
    if (!changed) return;

    amMacroLastInputStep = currentStep;
    amMacroHasLastInputStep = true;

    if (amMacroView == AM_MACRO_VIEW_HOME) {
        const int slot = getPressedMacroSlot(keyState);
        if (slot >= 0) {
            startMacroPlayback(slot, now);
            return;
        }

        if (macroCharPressed(keyState, 'r', 'R')) {
            beginMacroRecordingSelection(keyState);
            return;
        }

        if (macroCharPressed(keyState, 'l', 'L')) {
            openMacroList(keyState);
        }
        return;
    }

    if (amMacroView == AM_MACRO_VIEW_RECORD_SELECT) {
        const int slot = getPressedMacroSlot(keyState);
        if (slot >= 0) {
            beginMacroRecording(slot, keyState);
            return;
        }

        if (macroCharPressed(keyState, 'r', 'R') || macroBacktickPressed(keyState)) {
            openMacroHome(keyState);
            setMacroStatus("Record canceled", now, 1600);
            refreshAMUI();
        }
        return;
    }

    if (amMacroView == AM_MACRO_VIEW_RECORDING) {
        if (macroBacktickPressed(keyState)) {
            finishMacroRecording(now);
            return;
        }

        if (isMacroStepEmpty(currentStep) && amMacroRecordingSteps.empty()) return;

        if (amMacroRecordingSteps.size() >= AM_MACRO_MAX_STEPS) {
            amMacroRecordingTruncated = true;
            finishMacroRecording(now);
            return;
        }

        if (!isMacroStepEmpty(currentStep)) {
            String preview = getPreviewText(keyState);
            preview.trim();
            appendMacroPreviewToken(preview);
        }

        amMacroRecordingSteps.push_back(currentStep);
        refreshAMUI();
        return;
    }

    if (amMacroView == AM_MACRO_VIEW_LIST) {
        if (macroCharPressed(keyState, 'l', 'L') || macroBacktickPressed(keyState) || keyState.enter) {
            openMacroHome(keyState);
            return;
        }

        std::vector<String> lines;
        buildMacroListLines(lines);
        const int maxScroll = lines.size() > 7 ? static_cast<int>(lines.size()) - 7 : 0;

        if (M5Cardputer.Keyboard.isKeyPressed(';') && amMacroListScroll > 0) {
            --amMacroListScroll;
            refreshAMUI();
        } else if (M5Cardputer.Keyboard.isKeyPressed('.') && amMacroListScroll < maxScroll) {
            ++amMacroListScroll;
            refreshAMUI();
        }
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
    loadAMMacros();
    amInMenu = false;
    amMenuIndex = 0;
    amSettingsChanged = false;
    amControlMode = AM_MODE_MOUSE;
    amShowMouseHelp = false;
    amMacroMode = false;
    amBtnALongHandled = false;
    amMacroView = AM_MACRO_VIEW_HOME;
    amMacroListScroll = 0;
    amMacroPlaybackSlot = -1;
    amMacroPlaybackIndex = 0;
    amMacroPlaybackMillis = 0;
    amMacroStatusMessage = "";
    amMacroStatusUntil = 0;
    resetMacroRecordingBuffers();
    clearMacroInputTracking();
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

    refreshExpiredMacroStatus(keyState, now);

    if (!amInMenu && !amBtnALongHandled && M5.BtnA.pressedFor(AM_MACRO_HOLD_MS)) {
        amBtnALongHandled = true;
        if (amMacroMode) exitMacroMode(keyState);
        else enterMacroMode(keyState, now);
        return;
    }

    if (!amMacroMode) {
        flushPendingExitAction(keyState, now);
        if (handleExitGesture(keyState, now)) {
            if (M5.BtnA.wasReleased()) amBtnALongHandled = false;
            return;
        }
    }

    if (bleCombo.isConnected() != amWasConnected) {
        amWasConnected = bleCombo.isConnected();
        refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
    }

    if (amMacroMode) {
        handleMacroMode(keyState, now);
        if (M5.BtnA.wasReleased()) amBtnALongHandled = false;
        return;
    }

    if (!amInMenu && M5.BtnA.wasReleased() && !amBtnALongHandled) {
        toggleControlMode(keyState);
        return;
    }

    if (M5.BtnA.wasReleased()) amBtnALongHandled = false;

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
