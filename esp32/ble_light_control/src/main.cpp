// Replays captured 433MHz remote codes when a command byte is written to
// the CONTROL characteristic. Requires BLE bonding with a static passkey
// (MITM-protected) before either characteristic can be read/written --
// only a phone that has completed pairing once can control anything.
//
// Values captured from the real remote (see esp32/ble_capture_433):
//   0 light toggle: 8987533     5 fan stop: 9052303
//   1 timer 1h:     8859270     6 fan low:  9381258
//   2 timer 2h:     8793991     7 fan med:  8921740
//   3 timer 4h:     11731374    8 fan high: 10885307
//   4 timer 8h:     12258982
//
// Pairing: on first connection from a new phone, iOS will prompt for a
// 6-digit PIN -- enter STATIC_PASSKEY below. This only happens once per
// phone; afterwards the bond is cached on both sides.

#include <Arduino.h>
#include <RCSwitch.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

#define TRANSMIT_PIN (5)

#define STATIC_PASSKEY 435291  // change this to your own 6-digit number

#define SERVICE_UUID      "a1b2c3d4-0001-1000-8000-00805f9b34fb"
#define STATUS_CHAR_UUID  "a1b2c3d4-0002-1000-8000-00805f9b34fb"
#define CONTROL_CHAR_UUID "a1b2c3d4-0003-1000-8000-00805f9b34fb"

#define CODE_BIT_LENGTH   24
#define CODE_PULSE_LENGTH 232
#define CODE_PROTOCOL     1

struct Command {
  const char *name;
  unsigned long code;
};

const Command COMMANDS[] = {
    {"light toggle", 8987533UL},
    {"timer 1h",     8859270UL},
    {"timer 2h",     8793991UL},
    {"timer 4h",     11731374UL},
    {"timer 8h",     12258982UL},
    {"fan stop",     9052303UL},
    {"fan low",      9381258UL},
    {"fan med",      8921740UL},
    {"fan high",     10885307UL},
};
const uint8_t NUM_COMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

RCSwitch mySwitch = RCSwitch();
BLECharacteristic *statusChar;

void sendStatus(const String &msg) {
  statusChar->setValue((uint8_t *)msg.c_str(), msg.length());
  statusChar->notify();
  Serial.println(msg);
}

void runCommand(uint8_t index) {
  if (index >= NUM_COMMANDS) {
    sendStatus("unknown command index: " + String(index));
    return;
  }
  mySwitch.send(COMMANDS[index].code, CODE_BIT_LENGTH);
  sendStatus(String("sent: ") + COMMANDS[index].name);
}

class ControlCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *chr) override {
    std::string value = chr->getValue();
    if (value.length() > 0) {
      runCommand((uint8_t)value[0]);
    }
  }
};

class SecurityCallback : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return STATIC_PASSKEY; }
  void onPassKeyNotify(uint32_t pass_key) override {}
  bool onConfirmPIN(uint32_t pass_key) override { return true; }
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    sendStatus(cmpl.success ? "paired ok" : "pairing failed");
  }
};

void setup() {
  Serial.begin(115200);

  mySwitch.enableTransmit(TRANSMIT_PIN);
  mySwitch.setProtocol(CODE_PROTOCOL);
  mySwitch.setPulseLength(CODE_PULSE_LENGTH);

  BLEDevice::init("ESP32-Light");

  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
  BLEDevice::setSecurityCallbacks(new SecurityCallback());

  uint32_t passkey = STATIC_PASSKEY;
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_STATIC_PASSKEY, &passkey, sizeof(uint32_t));

  BLESecurity *security = new BLESecurity();
  security->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  security->setCapability(ESP_IO_CAP_OUT);
  security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);

  statusChar = service->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  statusChar->addDescriptor(new BLE2902());
  statusChar->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED);
  statusChar->setValue("ready");

  BLECharacteristic *controlChar = service->createCharacteristic(
      CONTROL_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE
  );
  controlChar->setAccessPermissions(ESP_GATT_PERM_WRITE_ENCRYPTED);
  controlChar->setCallbacks(new ControlCallback());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();

  Serial.println("Advertising as ESP32-Light, pairing required (PIN " + String(STATIC_PASSKEY) + ")");
}

void loop() {
}
