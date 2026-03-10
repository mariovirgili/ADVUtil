#include "BleComboHID.h"

#if defined(CONFIG_BT_ENABLED)

#include <NimBLEDevice.h>
#include <cstring>

#include "HIDTypes.h"

static const uint8_t kComboHidReportDescriptor[] = {
    USAGE_PAGE(1), 0x01,
    USAGE(1), 0x06,
    COLLECTION(1), 0x01,
    REPORT_ID(1), 0x01,
    USAGE_PAGE(1), 0x07,
    USAGE_MINIMUM(1), 0xE0,
    USAGE_MAXIMUM(1), 0xE7,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1), 0x01,
    REPORT_COUNT(1), 0x08,
    HIDINPUT(1), 0x02,
    REPORT_COUNT(1), 0x01,
    REPORT_SIZE(1), 0x08,
    HIDINPUT(1), 0x01,
    REPORT_COUNT(1), 0x05,
    REPORT_SIZE(1), 0x01,
    USAGE_PAGE(1), 0x08,
    USAGE_MINIMUM(1), 0x01,
    USAGE_MAXIMUM(1), 0x05,
    HIDOUTPUT(1), 0x02,
    REPORT_COUNT(1), 0x01,
    REPORT_SIZE(1), 0x03,
    HIDOUTPUT(1), 0x01,
    REPORT_COUNT(1), 0x06,
    REPORT_SIZE(1), 0x08,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x65,
    USAGE_PAGE(1), 0x07,
    USAGE_MINIMUM(1), 0x00,
    USAGE_MAXIMUM(1), 0x65,
    HIDINPUT(1), 0x00,
    END_COLLECTION(0),

    USAGE_PAGE(1), 0x01,
    USAGE(1), 0x02,
    COLLECTION(1), 0x01,
    REPORT_ID(1), 0x02,
    USAGE(1), 0x01,
    COLLECTION(1), 0x00,
    USAGE_PAGE(1), 0x09,
    USAGE_MINIMUM(1), 0x01,
    USAGE_MAXIMUM(1), 0x05,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1), 0x01,
    REPORT_COUNT(1), 0x05,
    HIDINPUT(1), 0x02,
    REPORT_SIZE(1), 0x03,
    REPORT_COUNT(1), 0x01,
    HIDINPUT(1), 0x03,
    USAGE_PAGE(1), 0x01,
    USAGE(1), 0x30,
    USAGE(1), 0x31,
    USAGE(1), 0x38,
    LOGICAL_MINIMUM(1), 0x81,
    LOGICAL_MAXIMUM(1), 0x7f,
    REPORT_SIZE(1), 0x08,
    REPORT_COUNT(1), 0x03,
    HIDINPUT(1), 0x06,
    USAGE_PAGE(1), 0x0c,
    USAGE(2), 0x38, 0x02,
    LOGICAL_MINIMUM(1), 0x81,
    LOGICAL_MAXIMUM(1), 0x7f,
    REPORT_SIZE(1), 0x08,
    REPORT_COUNT(1), 0x01,
    HIDINPUT(1), 0x06,
    END_COLLECTION(0),
    END_COLLECTION(0)
};

BleComboHID::BleComboHID(std::string deviceName, std::string deviceManufacturer, uint8_t batteryLevel)
    : batteryLevel(batteryLevel), deviceManufacturer(deviceManufacturer), deviceName(deviceName) {
}

void BleComboHID::begin() {
    NimBLEDevice::init(deviceName);
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_BOND);

    server = NimBLEDevice::createServer();
    server->setCallbacks(this);

    hid = new NimBLEHIDDevice(server);
    inputKeyboard = hid->getInputReport(0x01);
    outputKeyboard = hid->getOutputReport(0x01);
    inputMouse = hid->getInputReport(0x02);

    hid->setManufacturer(deviceManufacturer);
    hid->setPnp(0x02, 0xe502, 0xa111, 0x0210);
    hid->setHidInfo(0x00, 0x02);
    hid->setReportMap((uint8_t*)kComboHidReportDescriptor, sizeof(kComboHidReportDescriptor));
    hid->startServices();
    hid->setBatteryLevel(batteryLevel);

    NimBLEAdvertising* advertising = server->getAdvertising();
    advertising->setAppearance(GENERIC_HID);
    advertising->addServiceUUID(hid->getHidService()->getUUID());
    advertising->start();
}

void BleComboHID::end() {
}

bool BleComboHID::isConnected() const {
    return connected;
}

void BleComboHID::setBatteryLevel(uint8_t level) {
    batteryLevel = level;
    if (hid != nullptr) {
        hid->setBatteryLevel(level);
    }
}

void BleComboHID::move(signed char x, signed char y, signed char wheel, signed char hWheel) {
    if (!connected || inputMouse == nullptr) return;

    uint8_t report[5] = {
        mouseButtons,
        static_cast<uint8_t>(x),
        static_cast<uint8_t>(y),
        static_cast<uint8_t>(wheel),
        static_cast<uint8_t>(hWheel)
    };

    inputMouse->setValue(report, sizeof(report));
    inputMouse->notify();
}

void BleComboHID::pressMouse(uint8_t button) {
    updateMouseButtons(mouseButtons | button);
}

void BleComboHID::releaseMouse(uint8_t button) {
    updateMouseButtons(mouseButtons & ~button);
}

bool BleComboHID::isMousePressed(uint8_t button) const {
    return (mouseButtons & button) != 0;
}

void BleComboHID::releaseAllMouseButtons() {
    updateMouseButtons(0);
}

void BleComboHID::sendKeyboardReport(uint8_t modifiers, const uint8_t* keys, size_t keyCount) {
    uint8_t nextReport[8] = {0};
    nextReport[0] = modifiers;

    const size_t cappedCount = keyCount > 6 ? 6 : keyCount;
    for (size_t i = 0; i < cappedCount; ++i) {
        nextReport[2 + i] = keys[i];
    }

    if (std::memcmp(nextReport, keyboardReport, sizeof(keyboardReport)) == 0) return;

    std::memcpy(keyboardReport, nextReport, sizeof(keyboardReport));

    if (!connected || inputKeyboard == nullptr) return;

    inputKeyboard->setValue(keyboardReport, sizeof(keyboardReport));
    inputKeyboard->notify();
}

void BleComboHID::releaseKeyboard() {
    sendKeyboardReport(0, nullptr, 0);
}

void BleComboHID::onConnect(Callback cb) {
    connectCallback = cb;
}

void BleComboHID::onDisconnect(Callback cb) {
    disconnectCallback = cb;
}

void BleComboHID::onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) {
    connected = true;
    if (connectCallback) connectCallback();
}

void BleComboHID::onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) {
    connected = false;
    releaseAllMouseButtons();
    std::memset(keyboardReport, 0, sizeof(keyboardReport));
    if (disconnectCallback) disconnectCallback();
    if (pServer != nullptr) {
        pServer->getAdvertising()->start();
    }
}

void BleComboHID::updateMouseButtons(uint8_t buttons) {
    if (mouseButtons == buttons) return;
    mouseButtons = buttons;
    move(0, 0, 0, 0);
}

#endif
