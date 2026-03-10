#ifndef BLE_COMBO_HID_H
#define BLE_COMBO_HID_H

#include "sdkconfig.h"

#if defined(CONFIG_BT_ENABLED)

#include <NimBLECharacteristic.h>
#include <NimBLEHIDDevice.h>
#include <NimBLEServer.h>
#include <functional>

#define COMBO_MOUSE_LEFT 1
#define COMBO_MOUSE_RIGHT 2
#define COMBO_MOUSE_MIDDLE 4
#define COMBO_MOUSE_BACK 8
#define COMBO_MOUSE_FORWARD 16

class BleComboHID : protected NimBLEServerCallbacks {
public:
    using Callback = std::function<void(void)>;

    BleComboHID(std::string deviceName = "Cardputer Mouse", std::string deviceManufacturer = "M5Stack", uint8_t batteryLevel = 100);

    void begin();
    void end();

    bool isConnected() const;
    void setBatteryLevel(uint8_t level);

    void move(signed char x, signed char y, signed char wheel = 0, signed char hWheel = 0);
    void pressMouse(uint8_t button);
    void releaseMouse(uint8_t button);
    bool isMousePressed(uint8_t button) const;
    void releaseAllMouseButtons();

    void sendKeyboardReport(uint8_t modifiers, const uint8_t* keys, size_t keyCount);
    void releaseKeyboard();

    void onConnect(Callback cb);
    void onDisconnect(Callback cb);

protected:
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override;
    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override;

private:
    void updateMouseButtons(uint8_t buttons);

    uint8_t mouseButtons = 0;
    uint8_t keyboardReport[8] = {0};

    NimBLEHIDDevice* hid = nullptr;
    NimBLECharacteristic* inputKeyboard = nullptr;
    NimBLECharacteristic* outputKeyboard = nullptr;
    NimBLECharacteristic* inputMouse = nullptr;
    NimBLEServer* server = nullptr;

    uint8_t batteryLevel;
    std::string deviceManufacturer;
    std::string deviceName;
    bool connected = false;

    Callback connectCallback = nullptr;
    Callback disconnectCallback = nullptr;
};

#endif

#endif
