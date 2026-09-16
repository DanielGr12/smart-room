// Captures 433MHz remote codes using RCSwitch's protocol decoder, and
// reports them over a BLE notify characteristic (since USB serial was
// unreliable on this board). Unlike raw pin-toggle counting, this only
// reports when RCSwitch recognizes a structured, valid transmission --
// so ambient RF noise shouldn't produce spurious output.
//
// View it with a generic BLE scanner app (nRF Connect / LightBlue):
// connect to "ESP32-Debug", enable notifications on the one
// characteristic, set display format to UTF-8/Text, then press your
// remote's buttons and watch for decoded values.

#include <Arduino.h>
#include <RCSwitch.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define RECEIVE_PIN (4)

#define SERVICE_UUID        "a1b2c3d4-0001-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "a1b2c3d4-0002-1000-8000-00805f9b34fb"

RCSwitch mySwitch = RCSwitch();
BLECharacteristic *debugChar;

void sendBLE(const String &msg) {
  debugChar->setValue((uint8_t *)msg.c_str(), msg.length());
  debugChar->notify();
  Serial.println(msg);
}

void setup() {
  Serial.begin(115200);

  mySwitch.enableReceive(RECEIVE_PIN);

  BLEDevice::init("ESP32-Debug");
  BLEServer *server = BLEDevice::createServer();
  BLEService *service = server->createService(SERVICE_UUID);

  debugChar = service->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  debugChar->addDescriptor(new BLE2902());
  debugChar->setValue("booting...");

  service->start();

  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();

  sendBLE("Listening on 433MHz...");
}

void loop() {
  if (mySwitch.available()) {
    unsigned long value       = mySwitch.getReceivedValue();
    unsigned int  bitLength   = mySwitch.getReceivedBitlength();
    unsigned int  protocol    = mySwitch.getReceivedProtocol();
    unsigned int  pulseLength = mySwitch.getReceivedDelay();

    char buf[64];
    if (value == 0) {
      snprintf(buf, sizeof(buf), "unknown proto, bits:%u", bitLength);
    } else {
      snprintf(buf, sizeof(buf), "v:%lu b:%u p:%u pl:%u",
                value, bitLength, protocol, pulseLength);
    }
    sendBLE(String(buf));

    mySwitch.resetAvailable();
  }
}
