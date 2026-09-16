// Same raw pin-activity diagnostic as raw_pin_test, but reported over a
// BLE notify characteristic instead of Serial -- use this if the USB
// serial monitor isn't showing anything, to rule out a USB/serial-path
// problem versus an actual firmware/wiring problem.
//
// View it with any generic BLE scanner app (e.g. "nRF Connect" or
// "LightBlue" on iOS/Android): connect to "ESP32-Debug", open the one
// service, enable notifications on the one characteristic, and watch
// the text values roll in.

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define RECEIVE_PIN (4)

#define SERVICE_UUID        "a1b2c3d4-0001-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "a1b2c3d4-0002-1000-8000-00805f9b34fb"

BLECharacteristic *debugChar;

volatile unsigned long changeCount = 0;

void IRAM_ATTR onChange() {
  changeCount++;
}

void setup() {
  Serial.begin(115200);

  pinMode(RECEIVE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RECEIVE_PIN), onChange, CHANGE);

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

  Serial.println("BLE debug advertising as 'ESP32-Debug'");
}

unsigned long lastReportMs = 0;
unsigned long lastCount = 0;

void loop() {
  unsigned long now = millis();
  if (now - lastReportMs >= 1000) {
    lastReportMs = now;

    noInterrupts();
    unsigned long count = changeCount;
    interrupts();

    unsigned long delta = count - lastCount;
    lastCount = count;

    char buf[48];
    snprintf(buf, sizeof(buf), "tx/s:%lu pin:%d", delta, digitalRead(RECEIVE_PIN));

    debugChar->setValue((uint8_t *)buf, strlen(buf));
    debugChar->notify();

    Serial.println(buf);
  }
}
