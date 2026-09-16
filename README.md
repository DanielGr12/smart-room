# Smart Room Light — HomeKit-controlled 433MHz remote replacement

Replaces a physical 433MHz remote (light on/off, 4 timer presets, 4 fan
speeds) with native Apple HomeKit control — usable from the Home app,
Siri, Control Center, and automatically from the iOS Shortcuts app, with
no companion app, no paid third-party software, and no phone-side
developer setup required.

## How it works

An ESP32-C3 sniffs and replays the remote's 433MHz codes (via
[`rc-switch`](https://github.com/sui77/rc-switch)) and exposes each
button as a native HomeKit accessory (via
[HomeSpan](https://github.com/HomeSpan/HomeSpan)) — no bridge, no
server, no other always-on device involved.

```
[iPhone: Home app / Siri / Shortcuts]
              |  HomeKit Accessory Protocol (WiFi, encrypted)
              v
[ESP32-C3: HomeSpan + RCSwitch]
              |  433MHz OOK/ASK, replaying captured codes
              v
[Light + fan remote receiver]
```

Each of the 9 buttons is a "momentary switch" accessory: turning it on
fires the matching 433MHz code, then it auto-resets to off about half a
second later. That's deliberate — the remote's codes are fixed/toggle
codes with no feedback of real state, so pretending to track true
on/off state in HomeKit would just drift out of sync the moment the
original physical remote is used directly. A button-style accessory
makes no promise it can't keep.

## Hardware

- ESP32-C3 "SuperMini" board
- 433MHz receiver module — only needed temporarily, to capture your
  remote's codes (see `esp32/capture_433`)
- 433MHz transmitter module, wired to the pin in
  `esp32/homekit_light/src/main.cpp` (`TRANSMIT_PIN`)

## Setup

1. Flash `esp32/homekit_light` with PlatformIO (`pio run -t upload`).
2. Copy `esp32/homekit_light/include/secrets.h.example` to `secrets.h`
   in the same folder and fill in your own HomeKit setup code (8
   digits, no dashes, not sequential/repeating). This file is
   gitignored and must never be committed.
3. **WiFi credentials are never hardcoded.** On first boot with nothing
   stored in flash, the device opens its own temporary WiFi network
   (`RoomLight-Setup`) hosting a small web form. Connect your phone to
   it once, enter your real WiFi info there, and it's saved directly to
   the device's flash (NVS) — never touches source code or git. If you
   ever need to reprovision, trigger this again over the serial CLI
   (`A`) or by fully erasing flash.
4. In the Home app: **+** → **Add Accessory** → **More options...** →
   select the device → enter your setup code from step 2.
5. Once paired, all 9 accessories automatically appear as individual
   Shortcuts actions in the Shortcuts app — no additional setup needed.

WiFi credentials and HomeKit pairing are both stored in flash (NVS),
not RAM — they survive power loss and reboots indefinitely. Unplugging
the board doesn't require redoing any of the above.

## Protections in place

This device sits on your home WiFi, which was a deliberate, carefully
considered trade-off (see project history/conversation for the full
reasoning — several non-WiFi approaches were tried first and hit real
platform limitations). The following layers are specifically meant to
keep that exposure small and contained:

| Layer | What it does |
|---|---|
| **HomeKit Accessory Protocol (HAP)** | Pairing uses SRP-6a (3072-bit) — the setup code is never transmitted over the air. Sessions use Ed25519 identity keys + Curve25519 session keys, encrypted with ChaCha20-Poly1305, with perfect forward secrecy. This is the same protocol every certified "Works with Apple HomeKit" product uses. |
| **No BLE** | The firmware never initializes the Bluetooth stack — one less radio/attack surface, now that WiFi/HomeKit is the only control path. |
| **No OTA** | `homeSpan.enableOTA()` is intentionally never called — there is no remote firmware-update surface at all. |
| **No hardcoded credentials** | WiFi credentials never appear in source code (see Setup above). The HomeKit setup code lives in a gitignored `secrets.h`. |
| **No remote access (recommended)** | Don't add a Home Hub (Apple TV/HomePod/iPad) for this accessory and don't enable "Allow Remote Access." That keeps the WAN attack surface at literally zero — reachable only by something physically on your home WiFi. |
| **Network isolation (recommended, configure on your router)** | Put the device on an isolated VLAN/guest SSID with firewall rules blocking it from reaching any other device on your LAN, and blocking all outbound internet access. Even in a worst case, it has nowhere to pivot to and can't phone home. |

### Honest residual risks — not fixed by any of the above

- **The 433MHz replay itself is unauthenticated.** The remote uses a
  fixed, non-rolling code — anyone within RF range with a cheap
  transmitter could always replay it directly, bypassing WiFi,
  HomeKit, and every protection above entirely. This was true the day
  the physical remote was purchased and is a property of the
  light/fan hardware itself, not something firmware can fix.
- **HomeSpan is a community reimplementation of HAP**, not Apple's own
  certified/audited MFi stack. It's mature and widely used, but
  carries whatever implementation bugs exist in that project
  specifically.

Realistic worst case: someone toggles the light, sets a timer, or
changes the fan speed — either via the always-possible RF replay, or
in the unlikely case of an unpatched HomeSpan bug. Nothing in this
design gives an attacker a path to any other device on your network.

## Repo layout

- `esp32/homekit_light/` — **the current, working firmware.**
- `esp32/capture_433/` — standalone sketch to sniff a remote's codes
  (run this first, on any new remote).
- `esp32/ble_light_control/`, `esp32/ble_capture_433/`,
  `esp32/ble_pin_test/`, `esp32/raw_pin_test/` — earlier BLE-based
  prototypes and hardware diagnostics from before the project settled
  on HomeKit. Kept for reference; not part of the current setup.
- `ios/` — an alternate, unused companion-app approach (Swift +
  CoreBluetooth + App Intents) from the BLE era. Not needed with the
  current HomeKit-based firmware, kept for reference.
- `vision/` — an earlier, separate Raspberry Pi + camera gesture-based
  approach, superseded by the ESP32/HomeKit design above.
