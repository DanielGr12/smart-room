// ESP32 Super Mini (ESP32-C3)
// Sniffs 433MHz traffic and prints the decoded value/protocol/raw timing
// for every signal it hears. Run this once to capture your remote's
// ON/OFF codes, then unwire the receiver -- it's not needed at runtime.
//
// Wiring: receiver module DATA -> RECEIVE_PIN, VCC/GND per its datasheet.
// If your receiver runs its logic at 5V, check it's safe to wire DATA
// straight into an ESP32 GPIO (3.3V logic) before connecting it.

#include <Arduino.h>
#include <RCSwitch.h>

#define RECEIVE_PIN  (4)
#define TRANSMIT_PIN (5)  // unused here -- reserved for the transmit sketch

RCSwitch mySwitch = RCSwitch();

void setup() {
  Serial.begin(115200);
  delay(1000);

  mySwitch.enableReceive(RECEIVE_PIN);

  Serial.println("Listening on 433MHz. Press your remote's buttons now...");
}

void loop() {
  if (mySwitch.available()) {
    unsigned long value       = mySwitch.getReceivedValue();
    unsigned int  bitLength   = mySwitch.getReceivedBitlength();
    unsigned int  protocol    = mySwitch.getReceivedProtocol();
    unsigned int  pulseLength = mySwitch.getReceivedDelay();
    unsigned int* rawData     = mySwitch.getReceivedRawdata();

    Serial.println("----------------------------------------");

    if (value == 0) {
      Serial.println("Received signal, but it didn't match a known protocol.");
      Serial.print("Raw pulse count: ");
      Serial.println(bitLength * 2);
    } else {
      Serial.print("Value:        "); Serial.println(value);
      Serial.print("Bit length:   "); Serial.println(bitLength);
      Serial.print("Protocol:     "); Serial.println(protocol);
      Serial.print("Pulse length: "); Serial.println(pulseLength);
    }

    Serial.print("Raw data:     ");
    for (unsigned int i = 0; i < bitLength * 2; i++) {
      Serial.print(rawData[i]);
      Serial.print(",");
    }
    Serial.println();

    mySwitch.resetAvailable();
  }
}
