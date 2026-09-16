// Bypasses RCSwitch entirely -- just reports whether ANY electrical
// activity happens on RECEIVE_PIN, to tell wiring/power/range problems
// apart from RCSwitch protocol-decode problems.

#include <Arduino.h>

#define RECEIVE_PIN (4)

volatile unsigned long changeCount = 0;

void IRAM_ATTR onChange() {
  changeCount++;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(RECEIVE_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(RECEIVE_PIN), onChange, CHANGE);

  Serial.println("Raw pin monitor on GPIO4 -- reporting transition counts every second.");
  Serial.print("Idle pin state right now: ");
  Serial.println(digitalRead(RECEIVE_PIN));
  Serial.println("Press your remote's button repeatedly and watch the counts below.");
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

    Serial.print("transitions/sec: ");
    Serial.print(delta);
    Serial.print("   pin state: ");
    Serial.println(digitalRead(RECEIVE_PIN));
  }
}
