// Native HomeKit accessory (via HomeSpan) that replays captured 433MHz
// remote codes. Each of the 9 remote buttons is exposed as its own
// HomeKit "momentary switch" accessory: turning it on fires the RF
// code, then it auto-resets to off ~500ms later. That's deliberate --
// the remote only has fixed/toggle codes with no feedback of real
// state, so pretending to track true on/off state would just drift out
// of sync whenever the physical remote is used directly. A button-style
// accessory makes no promises it can't keep.
//
// Hardening notes (see conversation/README for full rationale):
//  - No BLE stack initialized anywhere in this firmware -- one less
//    radio/attack surface now that WiFi/HomeKit is the control path.
//  - OTA is never enabled (homeSpan.enableOTA() is intentionally not
//    called) -- no remote firmware-update surface at all.
//  - Put this device on an isolated VLAN/guest SSID with no route to
//    the rest of your LAN and no outbound internet access, and do NOT
//    add a Home Hub / enable remote access for it -- keeps this
//    reachable only from something physically on your home WiFi.
//  - No WiFi credentials are stored in source at all. On first boot with
//    nothing saved in flash, HomeSpan opens its own temporary "Setup"
//    WiFi network -- connect your phone to it once, fill in your real
//    WiFi info on the page it presents, and HomeSpan saves that to
//    flash (NVS) and reboots. See the README/conversation for the exact
//    steps. The HomeKit setup code still comes from include/secrets.h,
//    which is gitignored and must never be committed.

#include <Arduino.h>
#include <HomeSpan.h>
#include <RCSwitch.h>
#include "secrets.h"

#define TRANSMIT_PIN (5)

#define CODE_BIT_LENGTH   24
#define CODE_PULSE_LENGTH 232
#define CODE_PROTOCOL     1

#define NUM_COMMANDS 9
#define AUTO_OFF_MS  500

struct Command {
  const char *name;
  unsigned long code;
};

const Command COMMANDS[NUM_COMMANDS] = {
    {"Light Toggle", 8987533UL},
    {"Timer 1h",     8859270UL},
    {"Timer 2h",     8793991UL},
    {"Timer 4h",     11731374UL},
    {"Timer 8h",     12258982UL},
    {"Fan Stop",     9052303UL},
    {"Fan Low",      9381258UL},
    {"Fan Med",      8921740UL},
    {"Fan High",     10885307UL},
};

RCSwitch mySwitch = RCSwitch();

struct MomentaryButton : Service::Switch {
  SpanCharacteristic *power;
  uint8_t cmdIndex;
  unsigned long offAt = 0;

  explicit MomentaryButton(uint8_t index) : Service::Switch() {
    cmdIndex = index;
    power = new Characteristic::On(false);
  }

  boolean update() override {
    if (power->getNewVal<bool>()) {
      mySwitch.send(COMMANDS[cmdIndex].code, CODE_BIT_LENGTH);
      offAt = millis() + AUTO_OFF_MS;
    }
    return true;
  }

  void loop() override {
    if (offAt != 0 && millis() > offAt) {
      power->setVal<bool>(false);
      offAt = 0;
    }
  }
};

void setup() {
  Serial.begin(115200);

  mySwitch.enableTransmit(TRANSMIT_PIN);
  mySwitch.setProtocol(CODE_PROTOCOL);
  mySwitch.setPulseLength(CODE_PULSE_LENGTH);

  homeSpan.setPairingCode(HOMEKIT_SETUP_CODE);
  homeSpan.setApSSID("RoomLight-Setup");
  homeSpan.setApPassword("configure-me");
  homeSpan.enableAutoStartAP();
  homeSpan.begin(Category::Bridges, "Room Light");

  new SpanAccessory();                       // required bridge accessory (AID=1)
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new Characteristic::Name("Room Light Bridge");

  for (uint8_t i = 0; i < NUM_COMMANDS; i++) {
    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Name(COMMANDS[i].name);
    new MomentaryButton(i);
  }
}

void loop() {
  homeSpan.poll();
}
