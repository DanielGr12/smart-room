// Replays the captured light-toggle code over 433MHz when anything is
// written to the CONTROL characteristic. Values captured from the real
// remote (see esp32/ble_capture_433):
//
//   light toggle: v=8987533  b=24  p=1  pl=232
//   timer 1h:     8859270      timer 2h: 8793991
//   timer 4h:     11731374     timer 8h: 12258982
//   fan stop:     9052303      fan low:  9381258
//   fan med:      8921740      fan high: 10885307
//
// NOTE: this has no authentication yet -- any BLE device in range can
// currently write to CONTROL and toggle the light. Bonding/encryption
// still needs to be added before this is the final version.

#include <Arduino.h>
#include <RCSwitch.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define TRANSMIT_PIN (5)

#define LIGHT_TOGGLE_CODE   8987533UL
#define CODE_BIT_LENGTH     24
#define CODE_PULSE_LENGTH   232
#define CODE_PROTOCOL       1

#define SERVICE_UUID      "a1b2c3d4-0001-1000-8000-00805f9b34fb"
#define STATUS_CHAR_UUID  "a1b2c3d4-0002-1000-8000-00805f9b34fb"
#define CONTROL_CHAR_UUID "a1b2c3d4-0003-1000-8000-00805f9b34fb"

RCSwitch mySwitch = RCSwitch();
BLECharacteristic *statusChar;

void sendStatus(const String &msg) {
  statusChar->setValue((uint8_t *)msg.c_str(), msg.length());
  statusChar->notify();
  Serial.println(msg);
}

void toggleLight() {
  mySwitch.send(LIGHT_TOGGLE_CODE, CODE_BIT_LENGTH);
  sendStatus("toggled light");
}

class ControlCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *chr) override {
    std::string value = chr->getValue();
    if (value.length() > 0) {
      toggleLight();
    }
  }
};

void setup() {
  Serial.begin(115200);

  mySwitch.enableTransmit(TRANSMIT_PIN);
  mySwitch.setProtocol(CODE_PROTOCOL);
  mySwitch.setPulseLength(CODE_PULSE_LENGTH);

  BLEDevice::init("ESP32-Light");
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);

  statusChar = service->createCharacteristic(
      STATUS_CHAR_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  statusChar->addDescriptor(new BLE2902());
  statusChar->setValue("ready");

  BLECharacteristic *controlChar = service->createCharacteristic(
      CONTROL_CHAR_UUID,
      BLECharacteristic::PROPERTY_WRITE
  );
  controlChar->setCallbacks(new ControlCallback());

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();

  sendStatus("ready");
}

void loop() {
}
