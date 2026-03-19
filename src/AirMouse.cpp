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

enum AMKeyboardLayout : uint8_t {
    AM_LAYOUT_NONE = 0,
    AM_LAYOUT_US,
    AM_LAYOUT_IT,
    AM_LAYOUT_UK,
    AM_LAYOUT_FR,
    AM_LAYOUT_DE,
    AM_LAYOUT_DA,
    AM_LAYOUT_ES,
    AM_LAYOUT_HU,
    AM_LAYOUT_PT_BR,
    AM_LAYOUT_PT_PT,
    AM_LAYOUT_SV,
    AM_LAYOUT_COUNT
};

AMKeyboardLayout amKeyboardLayout = AM_LAYOUT_NONE;

enum AMMenuItem : int {
    AM_MENU_SENSITIVITY = 0,
    AM_MENU_INVERT_X,
    AM_MENU_INVERT_Y,
    AM_MENU_LAYOUT,
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

enum AMMacroKeyKind : uint8_t {
    AM_MACRO_KEY_NONE = 0,
    AM_MACRO_KEY_CHAR,
    AM_MACRO_KEY_ENTER,
    AM_MACRO_KEY_BACKSPACE,
    AM_MACRO_KEY_TAB,
    AM_MACRO_KEY_UP,
    AM_MACRO_KEY_LEFT,
    AM_MACRO_KEY_DOWN,
    AM_MACRO_KEY_RIGHT,
    AM_MACRO_KEY_HID
};

struct AMMacroKey {
    AMMacroKeyKind kind = AM_MACRO_KEY_NONE;
    uint8_t value = 0;
};

struct AMMacroStep {
    uint8_t modifiers = 0;
    AMMacroKey keys[6];
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
int amLayoutSelectionIndex = 0;
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
AMMacroStep amLastKeyboardStep;
bool amHasLastKeyboardStep = false;
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
constexpr uint8_t AM_HID_MOD_LEFT_CTRL = 0x01;
constexpr uint8_t AM_HID_MOD_LEFT_SHIFT = 0x02;
constexpr uint8_t AM_HID_MOD_LEFT_ALT = 0x04;
constexpr uint8_t AM_HID_MOD_RIGHT_ALT = 0x40;
constexpr uint16_t AM_LAYOUT_KEY_MASK = 0x00FF;
constexpr uint16_t AM_LAYOUT_SHIFT_MASK = 0x0100;
constexpr uint16_t AM_LAYOUT_ALTGR_MASK = 0x0200;
constexpr uint16_t AM_LAYOUT_DEADKEY_1_MASK = 0x0400;
constexpr uint16_t AM_LAYOUT_DEADKEY_2_MASK = 0x0800;
constexpr uint16_t AM_LAYOUT_DEADKEY_MASK = 0x0C00;

void clearExitArm();

static const uint16_t AM_LAYOUT_US_ASCII[] = {
    44, 286, 308, 288, 289, 290, 292, 52, 294, 295, 293, 302,
    54, 45, 55, 56, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 307, 51, 310, 46, 311, 312, 287, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 47,
    49, 48, 291, 301, 53, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 303, 305, 304, 309
};

static const uint16_t AM_LAYOUT_IT_ASCII[] = {
    44, 286, 287, 564, 289, 290, 291, 45, 293, 294, 304, 48,
    54, 56, 55, 292, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 311, 310, 50, 295, 306, 301, 563, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 559,
    53, 560, 302, 312, 0, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 0, 309, 0, 0
};

static const uint16_t AM_LAYOUT_UK_ASCII[] = {
    44, 286, 287, 50, 289, 290, 292, 52, 294, 295, 293, 302,
    54, 45, 55, 56, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 307, 51, 310, 46, 311, 312, 308, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 47,
    100, 48, 291, 301, 53, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 303, 356, 304, 306
};

static const uint16_t AM_LAYOUT_FR_ASCII[] = {
    44, 56, 32, 544, 48, 308, 30, 33, 34, 45, 49, 302,
    16, 35, 310, 311, 295, 286, 287, 288, 289, 290, 291, 292,
    293, 294, 55, 54, 50, 46, 306, 272, 551, 276, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 307, 273, 274,
    275, 260, 277, 278, 279, 280, 281, 285, 283, 284, 282, 546,
    549, 557, 550, 37, 548, 20, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 51, 17, 18, 19, 4, 21, 22,
    23, 24, 25, 29, 27, 28, 26, 545, 547, 558, 543
};

static const uint16_t AM_LAYOUT_DE_ASCII[] = {
    44, 286, 287, 49, 289, 290, 291, 305, 293, 294, 304, 48,
    54, 56, 55, 292, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 311, 310, 50, 295, 306, 301, 532, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 285, 284, 549,
    557, 550, 1068, 312, 2092, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 29, 28, 548, 562, 551, 560
};

static const uint16_t AM_LAYOUT_DA_ASCII[] = {
    44, 286, 287, 288, 545, 290, 291, 49, 293, 294, 305, 45,
    54, 56, 55, 292, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 311, 310, 50, 295, 306, 301, 543, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 549,
    562, 550, 0, 312, 0, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 548, 558, 551, 0
};

static const uint16_t AM_LAYOUT_ES_ASCII[] = {
    44, 286, 287, 544, 289, 290, 291, 45, 293, 294, 304, 48,
    54, 56, 55, 292, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 311, 310, 50, 295, 306, 301, 543, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 559,
    565, 560, 0, 312, 0, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 564, 542, 561, 0
};

static const uint16_t AM_LAYOUT_HU_ASCII[] = {
    44, 289, 287, 539, 563, 290, 518, 286, 293, 294, 568, 288,
    54, 56, 55, 291, 53, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 311, 566, 562, 292, 541, 310, 537, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 285, 284, 521,
    532, 522, 544, 312, 548, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 29, 28, 517, 538, 529, 542
};

static const uint16_t AM_LAYOUT_PT_BR_ASCII[] = {
    44, 286, 309, 288, 289, 290, 292, 53, 294, 295, 293, 302,
    54, 45, 55, 532, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 312, 56, 310, 46, 311, 538, 287, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 48,
    50, 49, 0, 301, 0, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 304, 306, 305, 0
};

static const uint16_t AM_LAYOUT_PT_PT_ASCII[] = {
    44, 286, 287, 288, 289, 290, 291, 45, 293, 294, 303, 47,
    54, 56, 55, 292, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 311, 310, 50, 295, 306, 301, 543, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 549,
    53, 550, 0, 312, 0, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 548, 309, 551, 0
};

static const uint16_t AM_LAYOUT_SV_ASCII[] = {
    44, 286, 287, 288, 545, 290, 291, 49, 293, 294, 305, 45,
    54, 56, 55, 292, 39, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 311, 310, 50, 295, 306, 301, 543, 260, 261, 262,
    263, 264, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274,
    275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 549,
    557, 550, 0, 312, 0, 4, 5, 6, 7, 8, 9, 10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 27, 28, 29, 548, 562, 551, 0
};

struct AMLayoutDefinition {
    AMKeyboardLayout layout;
    const char* configValue;
    const char* label;
    const uint16_t* asciiMap;
};

static const AMLayoutDefinition AM_LAYOUT_DEFINITIONS[] = {
    {AM_LAYOUT_US, "us", "US (QWERTY)", AM_LAYOUT_US_ASCII},
    {AM_LAYOUT_IT, "it", "IT (QWERTY)", AM_LAYOUT_IT_ASCII},
    {AM_LAYOUT_UK, "uk", "UK (QWERTY)", AM_LAYOUT_UK_ASCII},
    {AM_LAYOUT_FR, "fr", "FR (AZERTY)", AM_LAYOUT_FR_ASCII},
    {AM_LAYOUT_DE, "de", "DE (QWERTZ)", AM_LAYOUT_DE_ASCII},
    {AM_LAYOUT_DA, "da", "DA (QWERTY)", AM_LAYOUT_DA_ASCII},
    {AM_LAYOUT_ES, "es", "ES (QWERTY)", AM_LAYOUT_ES_ASCII},
    {AM_LAYOUT_HU, "hu", "HU (QWERTZ)", AM_LAYOUT_HU_ASCII},
    {AM_LAYOUT_PT_BR, "pt_br", "PT-BR (QWERTY)", AM_LAYOUT_PT_BR_ASCII},
    {AM_LAYOUT_PT_PT, "pt_pt", "PT-PT (QWERTY)", AM_LAYOUT_PT_PT_ASCII},
    {AM_LAYOUT_SV, "sv", "SV (QWERTY)", AM_LAYOUT_SV_ASCII},
};

constexpr int AM_LAYOUT_OPTION_COUNT =
    static_cast<int>(sizeof(AM_LAYOUT_DEFINITIONS) / sizeof(AM_LAYOUT_DEFINITIONS[0]));

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

const AMLayoutDefinition* findLayoutDefinition(AMKeyboardLayout layout) {
    for (const AMLayoutDefinition& definition : AM_LAYOUT_DEFINITIONS) {
        if (definition.layout == layout) return &definition;
    }
    return nullptr;
}

const char* getLayoutLabel(AMKeyboardLayout layout) {
    const AMLayoutDefinition* definition = findLayoutDefinition(layout);
    return definition ? definition->label : "Not set";
}

const char* getLayoutConfigValue(AMKeyboardLayout layout) {
    const AMLayoutDefinition* definition = findLayoutDefinition(layout);
    return definition ? definition->configValue : "none";
}

AMKeyboardLayout parseLayoutLabel(const String& value) {
    if (value.equalsIgnoreCase("en_us")) return AM_LAYOUT_US;
    if (value.equalsIgnoreCase("it_it")) return AM_LAYOUT_IT;
    if (value.equalsIgnoreCase("fr_fr")) return AM_LAYOUT_FR;
    if (value.equalsIgnoreCase("de_de")) return AM_LAYOUT_DE;
    if (value.equalsIgnoreCase("da_dk")) return AM_LAYOUT_DA;
    if (value.equalsIgnoreCase("es_es")) return AM_LAYOUT_ES;
    if (value.equalsIgnoreCase("hu_hu")) return AM_LAYOUT_HU;
    if (value.equalsIgnoreCase("pt_br")) return AM_LAYOUT_PT_BR;
    if (value.equalsIgnoreCase("pt_pt")) return AM_LAYOUT_PT_PT;
    if (value.equalsIgnoreCase("sv_se")) return AM_LAYOUT_SV;

    for (const AMLayoutDefinition& definition : AM_LAYOUT_DEFINITIONS) {
        if (value.equalsIgnoreCase(definition.configValue)) return definition.layout;
    }
    return AM_LAYOUT_NONE;
}

int getLayoutSelectionIndex(AMKeyboardLayout layout) {
    for (int i = 0; i < AM_LAYOUT_OPTION_COUNT; ++i) {
        if (AM_LAYOUT_DEFINITIONS[i].layout == layout) return i;
    }
    return 0;
}

AMKeyboardLayout getLayoutFromSelectionIndex(int index) {
    if (index < 0 || index >= AM_LAYOUT_OPTION_COUNT) return AM_LAYOUT_US;
    return AM_LAYOUT_DEFINITIONS[index].layout;
}

AMKeyboardLayout cycleLayout(AMKeyboardLayout current, int direction) {
    int next = getLayoutSelectionIndex(current == AM_LAYOUT_NONE ? AM_LAYOUT_US : current) + direction;
    while (next < 0) next += AM_LAYOUT_OPTION_COUNT;
    while (next >= AM_LAYOUT_OPTION_COUNT) next -= AM_LAYOUT_OPTION_COUNT;
    return getLayoutFromSelectionIndex(next);
}

const uint16_t* getLayoutAsciiMap(AMKeyboardLayout layout) {
    const AMLayoutDefinition* definition = findLayoutDefinition(layout);
    return definition ? definition->asciiMap : AM_LAYOUT_US_ASCII;
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
    } else if (key.equalsIgnoreCase("layout")) {
        amKeyboardLayout = parseLayoutLabel(value);
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
    amKeyboardLayout = AM_LAYOUT_NONE;
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
        writeConfigLine(file, "layout", getLayoutConfigValue(amKeyboardLayout));
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

uint16_t packMacroKey(const AMMacroKey& key) {
    return (static_cast<uint16_t>(key.kind) << 8) | key.value;
}

bool unpackMacroKey(uint16_t packed, AMMacroKey& key) {
    key.kind = static_cast<AMMacroKeyKind>((packed >> 8) & 0xFF);
    key.value = static_cast<uint8_t>(packed & 0xFF);
    return key.kind <= AM_MACRO_KEY_HID;
}

String encodeMacroSteps(const std::vector<AMMacroStep>& steps) {
    String encoded = "v2:";
    for (size_t i = 0; i < steps.size(); ++i) {
        if (i > 0) encoded += ',';
        encoded += toHexDigit((steps[i].modifiers >> 4) & 0x0F);
        encoded += toHexDigit(steps[i].modifiers & 0x0F);
        for (size_t key = 0; key < 6; ++key) {
            const uint16_t packed = packMacroKey(steps[i].keys[key]);
            encoded += toHexDigit((packed >> 12) & 0x0F);
            encoded += toHexDigit((packed >> 8) & 0x0F);
            encoded += toHexDigit((packed >> 4) & 0x0F);
            encoded += toHexDigit(packed & 0x0F);
        }
    }
    return encoded;
}

bool decodeMacroSteps(const String& text, std::vector<AMMacroStep>& steps) {
    steps.clear();
    if (text.length() == 0) return true;

    if (!text.startsWith("v2:")) {
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
                uint8_t hidKey = 0;
                if (!decodeHexByte(chunk, 2 + (i * 2), hidKey)) return false;
                if (hidKey == 0) continue;
                step.keys[i].kind = AM_MACRO_KEY_HID;
                step.keys[i].value = hidKey;
            }
            steps.push_back(step);
            start = end + 1;
        }

        return true;
    }

    const String payload = text.substring(3);
    int start = 0;
    while (start < payload.length()) {
        int end = payload.indexOf(',', start);
        if (end < 0) end = payload.length();

        String chunk = payload.substring(start, end);
        chunk.trim();
        if (chunk.length() != 26) return false;

        AMMacroStep step;
        if (!decodeHexByte(chunk, 0, step.modifiers)) return false;
        for (int i = 0; i < 6; ++i) {
            uint8_t high = 0;
            uint8_t low = 0;
            if (!decodeHexByte(chunk, 2 + (i * 4), high)) return false;
            if (!decodeHexByte(chunk, 4 + (i * 4), low)) return false;
            if (!unpackMacroKey(static_cast<uint16_t>((high << 8) | low), step.keys[i])) return false;
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

AMMacroKeyKind getFnArrowKeyKind(char key) {
    switch (key) {
        case ';': return AM_MACRO_KEY_UP;
        case ',': return AM_MACRO_KEY_LEFT;
        case '.': return AM_MACRO_KEY_DOWN;
        case '/': return AM_MACRO_KEY_RIGHT;
        default:  return AM_MACRO_KEY_NONE;
    }
}

void setMacroKey(AMMacroKey& key, AMMacroKeyKind kind, uint8_t value = 0) {
    key.kind = kind;
    key.value = value;
}

bool macroKeyEquals(const AMMacroKey& left, const AMMacroKey& right) {
    return left.kind == right.kind && left.value == right.value;
}

const char* getMacroKeyLabel(const AMMacroKey& key) {
    switch (key.kind) {
        case AM_MACRO_KEY_ENTER: return "Enter";
        case AM_MACRO_KEY_BACKSPACE: return "Backspace";
        case AM_MACRO_KEY_TAB: return "Tab";
        case AM_MACRO_KEY_UP: return "Up";
        case AM_MACRO_KEY_LEFT: return "Left";
        case AM_MACRO_KEY_DOWN: return "Down";
        case AM_MACRO_KEY_RIGHT: return "Right";
        case AM_MACRO_KEY_HID: return "Key";
        case AM_MACRO_KEY_CHAR:
        case AM_MACRO_KEY_NONE:
        default:
            return nullptr;
    }
}

AMMacroStep buildMacroStep(const Keyboard_Class::KeysState& keyState) {
    AMMacroStep step;
    size_t keyCount = 0;
    bool hasCharKey = false;

    if (keyState.tab && keyCount < 6) setMacroKey(step.keys[keyCount++], AM_MACRO_KEY_TAB);
    if (keyState.del && keyCount < 6) setMacroKey(step.keys[keyCount++], AM_MACRO_KEY_BACKSPACE);
    if (keyState.enter && keyCount < 6) setMacroKey(step.keys[keyCount++], AM_MACRO_KEY_ENTER);

    for (char logicalChar : keyState.word) {
        if (keyCount >= 6) break;

        if (keyState.fn) {
            const AMMacroKeyKind arrowKind = getFnArrowKeyKind(logicalChar);
            if (arrowKind != AM_MACRO_KEY_NONE) {
                setMacroKey(step.keys[keyCount++], arrowKind);
                continue;
            }
        }

        setMacroKey(step.keys[keyCount++], AM_MACRO_KEY_CHAR, static_cast<uint8_t>(logicalChar));
        hasCharKey = true;
    }

    if (keyState.ctrl) step.modifiers |= AM_HID_MOD_LEFT_CTRL;
    if (keyState.alt) step.modifiers |= AM_HID_MOD_LEFT_ALT;
    if (keyState.shift && !hasCharKey) step.modifiers |= AM_HID_MOD_LEFT_SHIFT;

    return step;
}

String getStepPreview(const AMMacroStep& step) {
    if (step.modifiers == 0) {
        bool hasKeys = false;
        for (const AMMacroKey& key : step.keys) {
            if (key.kind != AM_MACRO_KEY_NONE) {
                hasKeys = true;
                break;
            }
        }
        if (!hasKeys) return "Ready to type";
    }

    String preview = "";
    if (step.modifiers & AM_HID_MOD_LEFT_CTRL) preview += "Ctrl ";
    if (step.modifiers & AM_HID_MOD_LEFT_SHIFT) preview += "Shift ";
    if (step.modifiers & AM_HID_MOD_LEFT_ALT) preview += "Alt ";

    for (const AMMacroKey& key : step.keys) {
        if (key.kind == AM_MACRO_KEY_NONE) continue;
        if (preview.length() >= 24) break;
        if (preview.length() > 0) preview += ' ';

        if (key.kind == AM_MACRO_KEY_CHAR) {
            if (key.value == ' ') preview += "Space";
            else preview += static_cast<char>(key.value);
            continue;
        }

        const char* label = getMacroKeyLabel(key);
        if (label != nullptr) preview += label;
    }

    return preview.length() ? preview : "Ready to type";
}

String getPreviewText(const Keyboard_Class::KeysState& keyState) {
    return getStepPreview(buildMacroStep(keyState));
}

bool macroStepEquals(const AMMacroStep& left, const AMMacroStep& right) {
    if (left.modifiers != right.modifiers) return false;
    for (int i = 0; i < 6; ++i) {
        if (!macroKeyEquals(left.keys[i], right.keys[i])) return false;
    }
    return true;
}

bool isMacroStepEmpty(const AMMacroStep& step) {
    if (step.modifiers != 0) return false;
    for (int i = 0; i < 6; ++i) {
        if (step.keys[i].kind != AM_MACRO_KEY_NONE) return false;
    }
    return true;
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

bool getLayoutDeadKeyStroke(AMKeyboardLayout layout, uint16_t deadKeyBits, uint8_t& modifiers, uint8_t& key) {
    modifiers = 0;
    key = 0;

    switch (layout) {
        case AM_LAYOUT_DE:
            if (deadKeyBits == AM_LAYOUT_DEADKEY_1_MASK) {
                key = 53;
                return true;
            }
            if (deadKeyBits == AM_LAYOUT_DEADKEY_2_MASK) {
                modifiers = AM_HID_MOD_LEFT_SHIFT;
                key = 46;
                return true;
            }
            return false;
        default:
            return false;
    }
}

bool translateLayoutChar(char c, uint8_t& modifiers, uint8_t& key, bool& usesDeadKey, uint8_t& deadModifiers, uint8_t& deadKey) {
    modifiers = 0;
    key = 0;
    usesDeadKey = false;
    deadModifiers = 0;
    deadKey = 0;

    if (c < 32 || c > 126) return false;

    const uint16_t raw = getLayoutAsciiMap(amKeyboardLayout)[static_cast<uint8_t>(c) - 32];
    if (raw == 0) return false;

    const uint16_t deadKeyBits = raw & AM_LAYOUT_DEADKEY_MASK;
    if (deadKeyBits != 0) {
        if (!getLayoutDeadKeyStroke(amKeyboardLayout, deadKeyBits, deadModifiers, deadKey)) return false;
        usesDeadKey = true;
    }

    if (raw & AM_LAYOUT_SHIFT_MASK) modifiers |= AM_HID_MOD_LEFT_SHIFT;
    if (raw & AM_LAYOUT_ALTGR_MASK) modifiers |= AM_HID_MOD_RIGHT_ALT;
    key = static_cast<uint8_t>(raw & AM_LAYOUT_KEY_MASK);
    return key != 0;
}

bool translateMacroKeyToHid(const AMMacroKey& keyToken, uint8_t& modifiers, uint8_t& key, bool& usesDeadKey,
                            uint8_t& deadModifiers, uint8_t& deadKey) {
    modifiers = 0;
    key = 0;
    usesDeadKey = false;
    deadModifiers = 0;
    deadKey = 0;

    switch (keyToken.kind) {
        case AM_MACRO_KEY_NONE:
            return false;
        case AM_MACRO_KEY_CHAR:
            return translateLayoutChar(static_cast<char>(keyToken.value), modifiers, key, usesDeadKey, deadModifiers, deadKey);
        case AM_MACRO_KEY_ENTER:
            key = KEY_ENTER;
            return true;
        case AM_MACRO_KEY_BACKSPACE:
            key = KEY_BACKSPACE;
            return true;
        case AM_MACRO_KEY_TAB:
            key = KEY_TAB;
            return true;
        case AM_MACRO_KEY_UP:
            key = AM_HID_UP_ARROW;
            return true;
        case AM_MACRO_KEY_LEFT:
            key = AM_HID_LEFT_ARROW;
            return true;
        case AM_MACRO_KEY_DOWN:
            key = AM_HID_DOWN_ARROW;
            return true;
        case AM_MACRO_KEY_RIGHT:
            key = AM_HID_RIGHT_ARROW;
            return true;
        case AM_MACRO_KEY_HID:
            key = keyToken.value;
            return key != 0;
        default:
            return false;
    }
}

bool stepRequiresSequence(const AMMacroStep& step) {
    for (const AMMacroKey& keyToken : step.keys) {
        if (keyToken.kind != AM_MACRO_KEY_CHAR) continue;

        uint8_t modifiers = 0;
        uint8_t key = 0;
        uint8_t deadModifiers = 0;
        uint8_t deadKey = 0;
        bool usesDeadKey = false;
        if (!translateLayoutChar(static_cast<char>(keyToken.value), modifiers, key, usesDeadKey, deadModifiers, deadKey)) {
            return true;
        }
        if (usesDeadKey) return true;
    }

    return false;
}

bool buildTranslatedKeyboardReport(const AMMacroStep& step, uint8_t& modifiers, uint8_t keys[6], size_t& keyCount) {
    modifiers = step.modifiers;
    keyCount = 0;
    for (int i = 0; i < 6; ++i) keys[i] = 0;

    for (const AMMacroKey& keyToken : step.keys) {
        if (keyToken.kind == AM_MACRO_KEY_NONE) continue;

        uint8_t keyModifiers = 0;
        uint8_t key = 0;
        uint8_t deadModifiers = 0;
        uint8_t deadKey = 0;
        bool usesDeadKey = false;
        if (!translateMacroKeyToHid(keyToken, keyModifiers, key, usesDeadKey, deadModifiers, deadKey)) continue;
        if (usesDeadKey) return false;

        modifiers |= keyModifiers;
        if (keyCount < 6) keys[keyCount++] = key;
    }

    return true;
}

void sendKeyboardTap(uint8_t modifiers, uint8_t key) {
    const uint8_t keys[1] = {key};
    bleCombo.sendKeyboardReport(modifiers, keys, 1);
    delay(12);
    bleCombo.releaseKeyboard();
    delay(12);
}

void playTranslatedSequence(const AMMacroStep& step) {
    for (const AMMacroKey& keyToken : step.keys) {
        if (keyToken.kind == AM_MACRO_KEY_NONE) continue;

        uint8_t keyModifiers = 0;
        uint8_t key = 0;
        uint8_t deadModifiers = 0;
        uint8_t deadKey = 0;
        bool usesDeadKey = false;
        if (!translateMacroKeyToHid(keyToken, keyModifiers, key, usesDeadKey, deadModifiers, deadKey)) continue;

        if (usesDeadKey) sendKeyboardTap(deadModifiers, deadKey);
        sendKeyboardTap(step.modifiers | keyModifiers, key);
    }
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

void drawAMLayoutSetup() {
    drawAMBackground();
    drawAMHeader("First Time Setup", "Choose the host keyboard layout");

    const int visibleCount = 5;
    const int maxFirstVisible = AM_LAYOUT_OPTION_COUNT > visibleCount ? AM_LAYOUT_OPTION_COUNT - visibleCount : 0;
    int firstVisible = amLayoutSelectionIndex - (visibleCount / 2);
    if (firstVisible < 0) firstVisible = 0;
    if (firstVisible > maxFirstVisible) firstVisible = maxFirstVisible;

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_YELLOW);
    const String counter = String(amLayoutSelectionIndex + 1) + "/" + String(AM_LAYOUT_OPTION_COUNT);
    const int counterX = M5.Display.width() - (counter.length() * 6) - 16;
    M5.Display.setCursor(counterX, 22);
    M5.Display.print(counter);

    int y = 34;
    for (int i = 0; i < visibleCount && (firstVisible + i) < AM_LAYOUT_OPTION_COUNT; ++i) {
        const int optionIndex = firstVisible + i;
        const bool selected = optionIndex == amLayoutSelectionIndex;
        drawAMCard(12, y, 216, 16, selected);
        drawAMCardTitle(20, y + 5, AM_LAYOUT_DEFINITIONS[optionIndex].label, selected);
        y += 18;
    }

    drawAMFooter(";/. navigate", "Ent or BtnA save", TFT_YELLOW);
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
    drawWrappedTextBlock("Press 1..0 to overwrite or create the corresponding macro slot. Press R or ESC to cancel.",
                         20, 54, 32, 4, WHITE);

    drawAMCard(10, 102, 220, 16, false);
    drawAMCardTitle(20, 107, "Filled", false);
    drawAMCardValue(62, 107, String(getUsedMacroCount()) + " saved macros", false);
}

void drawAMMacroRecording() {
    const String title = String("Recording Slot ") + getMacroSlotLabel(amMacroSelectedSlot);
    drawAMBackground();
    drawAMHeader(title.c_str(), "Press ESC to stop and save");

    drawAMCard(10, 34, 220, 84, false);
    drawAMCardTitle(20, 41, "Captured", false);

    const String content = amMacroRecordingPreview.length() ? amMacroRecordingPreview : String("Press keys to start recording");
    drawWrappedTextBlock(content, 20, 54, 32, 5, WHITE);

    M5.Display.setTextColor(M5.Display.color565(220, 245, 255));
    M5.Display.setCursor(20, 108);
    M5.Display.printf("Steps: %d", static_cast<int>(amMacroRecordingSteps.size()));

    drawAMFooter("Hold BtnA exits", "ESC save macro", TFT_YELLOW);
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

    drawAMFooter(";/. scroll", "L or ESC close", M5.Display.color565(220, 245, 255));
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
        "Layout",
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
        getLayoutLabel(amKeyboardLayout),
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
    if (amKeyboardLayout == AM_LAYOUT_NONE) drawAMLayoutSetup();
    else if (amMacroMode) drawAMMacroUI();
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
        case AM_MENU_LAYOUT:
            amKeyboardLayout = cycleLayout(amKeyboardLayout, direction);
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
    if (stepRequiresSequence(step)) {
        playTranslatedSequence(step);
    } else {
        uint8_t modifiers = 0;
        uint8_t keys[6] = {0};
        size_t keyCount = 0;
        if (buildTranslatedKeyboardReport(step, modifiers, keys, keyCount)) {
            bleCombo.sendKeyboardReport(modifiers, keys, keyCount);
        } else {
            bleCombo.releaseKeyboard();
        }
    }
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
    amHasLastKeyboardStep = false;
    refreshAMUI(amControlMode == AM_MODE_KEYBOARD ? &keyState : nullptr);
}

void confirmKeyboardLayoutSelection() {
    amKeyboardLayout = getLayoutFromSelectionIndex(amLayoutSelectionIndex);
    saveAMSettings();
    amSettingsChanged = false;
    amLastKeyboardPreview = "";
    amLastKeyboardHintVisible = false;
    amHasLastKeyboardStep = false;
    clearExitArm();
    releaseAllAMButtons();
    refreshAMUI();
}

void handleLayoutSetup(unsigned long now) {
    if (now - amLastKeyPress <= 180) return;

    if (M5Cardputer.Keyboard.isKeyPressed(';')) {
        amLayoutSelectionIndex = (amLayoutSelectionIndex + AM_LAYOUT_OPTION_COUNT - 1) % AM_LAYOUT_OPTION_COUNT;
        refreshAMUI();
        amLastKeyPress = now;
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed('.')) {
        amLayoutSelectionIndex = (amLayoutSelectionIndex + 1) % AM_LAYOUT_OPTION_COUNT;
        refreshAMUI();
        amLastKeyPress = now;
        return;
    }

    if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) || M5.BtnA.isPressed()) {
        confirmKeyboardLayoutSelection();
        amLastKeyPress = now;
    }
}

void handleKeyboardMode(const Keyboard_Class::KeysState& keyState) {
    const AMMacroStep currentStep = buildMacroStep(keyState);
    const bool changed = !amHasLastKeyboardStep || !macroStepEquals(currentStep, amLastKeyboardStep);

    if (changed) {
        amLastKeyboardStep = currentStep;
        amHasLastKeyboardStep = true;

        if (isMacroStepEmpty(currentStep)) {
            bleCombo.releaseKeyboard();
        } else if (stepRequiresSequence(currentStep)) {
            playTranslatedSequence(currentStep);
        } else {
            uint8_t modifiers = 0;
            uint8_t keys[6] = {0};
            size_t keyCount = 0;
            if (buildTranslatedKeyboardReport(currentStep, modifiers, keys, keyCount)) {
                bleCombo.sendKeyboardReport(modifiers, keys, keyCount);
            } else {
                bleCombo.releaseKeyboard();
            }
        }
    }

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
    amHasLastKeyboardStep = false;
    amWasConnected = bleCombo.isConnected();
    amDelWasPressed = false;
    clearExitArm();
    fractionX = 0.0f;
    fractionY = 0.0f;
    amLayoutSelectionIndex = getLayoutSelectionIndex(amKeyboardLayout);
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

    if (amKeyboardLayout == AM_LAYOUT_NONE) {
        handleLayoutSetup(now);
        if (M5.BtnA.wasReleased()) amBtnALongHandled = false;
        return;
    }

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

bool airMouseBlocksExit() {
    return amKeyboardLayout == AM_LAYOUT_NONE;
}
