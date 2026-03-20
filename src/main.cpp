#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include "AirMouse.h"
#include "Gamepad.h"
#include "GPSInfo.h"

// Global SD Card variables
SPIClass spiSD(HSPI);
bool sdAvailable = false;
bool returnToMenu = false;

enum AppState { STATE_MENU, STATE_AIR_MOUSE, STATE_GAMEPAD, STATE_GPS_INFO };
enum HidStack {
    HID_STACK_NONE = 0,
    HID_STACK_AIR_MOUSE,
    HID_STACK_GAMEPAD
};

AppState currentState = STATE_MENU;
HidStack activeHidStack = HID_STACK_NONE;

int mainMenuIndex = 0;
const int numMenuItems = 3;
unsigned long mainLastKeyPress = 0;
unsigned long menuStatusUntil = 0;
String menuStatusMessage = "";
constexpr unsigned long MENU_STATUS_MS = 2200;

// Helper smooth gradient
uint16_t getGradientColor(int step, int totalSteps) {
    int r0 = 100, g0 = 200, b0 = 255; // Celeste
    int r1 = 0,   g1 = 70,  b1 = 150; // Azzurro
    int r = r0 + (r1 - r0) * step / totalSteps;
    int g = g0 + (g1 - g0) * step / totalSteps;
    int b = b0 + (b1 - b0) * step / totalSteps;
    return M5.Display.color565(r, g, b);
}

void drawIcon(int index, int x, int y, uint16_t color) {
    if (index == 0) { // Air Mouse
        M5.Display.drawRoundRect(x, y, 16, 24, 6, color);
        M5.Display.drawLine(x + 8, y, x + 8, y + 10, color);
        M5.Display.fillRoundRect(x + 6, y + 3, 4, 6, 2, color);
    } else if (index == 1) { // Gamepad
        M5.Display.drawRoundRect(x, y + 4, 18, 14, 6, color);
        M5.Display.drawLine(x + 5, y + 11, x + 9, y + 11, color);
        M5.Display.drawLine(x + 7, y + 9, x + 7, y + 13, color);
        M5.Display.drawCircle(x + 13, y + 9, 1, color);
        M5.Display.drawCircle(x + 15, y + 13, 1, color);
    } else if (index == 2) { // GPS
        M5.Display.fillCircle(x + 8, y + 8, 7, color);
        M5.Display.fillTriangle(x + 1, y + 12, x + 15, y + 12, x + 8, y + 24, color);
        M5.Display.fillCircle(x + 8, y + 8, 3, BLACK);
    }
}

void setupSD() {
    spiSD.begin(40, 39, 14, 12);
    if (SD.begin(12, spiSD)) {
        sdAvailable = true;
        if (!SD.exists("/ADVUtil")) SD.mkdir("/ADVUtil");
    } else {
        sdAvailable = false;
    }
}

void setMenuStatus(const String& message) {
    menuStatusMessage = message;
    menuStatusUntil = millis() + MENU_STATUS_MS;
}

void drawMenu() {
    const char* menuTitle = "ADVUtil v0.6 - MENU";

    for (int i = 0; i < M5.Display.height(); i++) {
        M5.Display.drawGradientLine(0, i, M5.Display.width(), i, getGradientColor(i, M5.Display.height()), getGradientColor(i, M5.Display.height()));
    }

    M5.Display.setTextSize(2);
    const int titleWidth = strlen(menuTitle) * 12;
    const int titleX = (M5.Display.width() - titleWidth) / 2;
    M5.Display.setTextColor(BLACK);
    M5.Display.setCursor(titleX + 2, 12);
    M5.Display.println(menuTitle);
    M5.Display.setTextColor(WHITE);
    M5.Display.setCursor(titleX, 10);
    M5.Display.println(menuTitle);

    int startY = 42;
    int spacing = 28;

    for (int i = 0; i < numMenuItems; i++) {
        int itemY = startY + (i * spacing);
        uint16_t textColor = WHITE;
        uint16_t iconColor = WHITE;
        
        if (mainMenuIndex == i) {
            M5.Display.fillRoundRect(10, itemY - 8, M5.Display.width() - 20, 34, 8, WHITE);
            textColor = BLACK;
            iconColor = M5.Display.color565(0, 100, 200);
        }

        drawIcon(i, 25, itemY - 3, iconColor);
        M5.Display.setTextSize(2);
        M5.Display.setTextColor(textColor);
        M5.Display.setCursor(55, itemY);
        if (i == 0) M5.Display.println("Air Mouse/Kbd");
        if (i == 1) M5.Display.println("Gamepad");
        if (i == 2) M5.Display.println("GPS Info");
    }

    M5.Display.setTextSize(1);
    M5.Display.setTextColor(M5.Display.color565(200, 240, 255));
    M5.Display.setCursor(15, 122);
    M5.Display.printf("SD: %s", sdAvailable ? "OK" : "N/A");
    
    M5.Display.setCursor(82, 122);
    if (menuStatusMessage.length() && millis() < menuStatusUntil) {
        M5.Display.println(menuStatusMessage);
    } else {
        M5.Display.println(";/. nav | Ent start");
    }
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5.Display.setRotation(1);
    
    setupSD();
    currentState = STATE_MENU;
    gpsInfoInit();
    drawMenu();
}

void loop() {
    M5Cardputer.update();

    if (currentState == STATE_MENU) {
        if (menuStatusMessage.length() && millis() >= menuStatusUntil) {
            menuStatusMessage = "";
            drawMenu();
        }

        if (millis() - mainLastKeyPress > 150) {
            bool redraw = false;
            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                mainMenuIndex = (mainMenuIndex - 1 + numMenuItems) % numMenuItems;
                redraw = true;
                mainLastKeyPress = millis();
            } else if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                mainMenuIndex = (mainMenuIndex + 1) % numMenuItems;
                redraw = true;
                mainLastKeyPress = millis();
            } else if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER) || M5.BtnA.isPressed()) {
                if (mainMenuIndex == 0) {
                    if (activeHidStack == HID_STACK_GAMEPAD) {
                        setMenuStatus("Reboot to use AirMouse");
                        redraw = true;
                    } else {
                        if (activeHidStack == HID_STACK_NONE) {
                            airMouseInit();
                            activeHidStack = HID_STACK_AIR_MOUSE;
                        }
                        currentState = STATE_AIR_MOUSE;
                        airMouseResetUI();
                    }
                } else if (mainMenuIndex == 1) {
                    if (activeHidStack == HID_STACK_AIR_MOUSE) {
                        setMenuStatus("Reboot to use Gamepad");
                        redraw = true;
                    } else {
                        if (activeHidStack == HID_STACK_NONE) {
                            gamepadInit();
                            activeHidStack = HID_STACK_GAMEPAD;
                        }
                        currentState = STATE_GAMEPAD;
                        gamepadResetUI();
                    }
                } else if (mainMenuIndex == 2) {
                    currentState = STATE_GPS_INFO;
                    gpsInfoResetUI();
                }
                mainLastKeyPress = millis();
            }
            if (redraw) drawMenu();
        }
    } else {
        const bool wantsQuit = currentState == STATE_GPS_INFO && M5Cardputer.Keyboard.isKeyPressed('q');
        if (returnToMenu) {
            if (currentState == STATE_GPS_INFO) gpsInfoTeardown();
            currentState = STATE_MENU;
            returnToMenu = false;
            drawMenu();
            mainLastKeyPress = millis();
            return;
        }

        if (wantsQuit) {
            gpsInfoTeardown();
            currentState = STATE_MENU;
            drawMenu();
            mainLastKeyPress = millis();
            return;
        }
        
        if (currentState == STATE_AIR_MOUSE) airMouseLoop();
        else if (currentState == STATE_GAMEPAD) gamepadLoop();
        else if (currentState == STATE_GPS_INFO) gpsInfoLoop();
    }
    delay(10);
}
