# Gesture-to-Light (Raspberry Pi + 433MHz)

Raise your hand, held for ~1s, anywhere in the room (even in the dark) → a
Raspberry Pi running local pose detection spots it and directly transmits
your light remote's 433MHz code from its own GPIO pins. Single board, no
microcontroller needed.

## Parts

- Raspberry Pi with WiFi/camera support (Zero 2 W is plenty)
- Raspberry Pi Camera Module, **NoIR** variant (no IR-cut filter → works in the dark) + the correct CSI ribbon cable for your Pi
- A small 850nm/940nm IR illuminator board (CCTV night-vision type, often has an auto light-sensor switch)
- 433MHz receiver module (e.g. RXB6) — only needed temporarily, for sniffing your remote's code
- 433MHz transmitter module (e.g. FS1000A)
- microSD card, 5V power supply

## Architecture

```
[IR illuminator + NoIR camera] -> [Raspberry Pi: MediaPipe Pose, local only]
                                          |
                                   direct GPIO (rpi-rf)
                                          v
                              [433MHz TX module -> your light]
```

Everything — camera capture, pose detection, and the RF transmit — runs on
the one Pi. No cloud vision API, no network hop for the trigger.

## Wiring (BCM GPIO numbering)

| Module              | Pin  | Raspberry Pi |
|---------------------|------|--------------|
| 433MHz RX (sniffing)| DATA | GPIO27       |
| 433MHz TX           | DATA | GPIO17       |

Power the RX/TX modules per their datasheet (most run on 5V, with 3.3V-logic-compatible DATA pins — double check yours before wiring DATA straight to a GPIO). Share GND with the Pi.

## 1. Set up the Pi

Use 64-bit Raspberry Pi OS (needed for MediaPipe wheels). Enable the camera
in `raspi-config`, then:

```
cd vision
python3 -m venv venv --system-site-packages
source venv/bin/activate
pip install -r requirements.txt
pip install picamera2  # if not already present system-wide
```

`rpi-rf` talks to GPIO via RPi.GPIO, which needs either root or the `gpio`
group — run sniff.py/gesture_trigger.py with `sudo` or add your user to
that group.

## 2. Sniff your light remote's code

Wire the 433MHz receiver (GPIO27), then:

```
python3 sniff.py
```

Press your real remote's ON button several times near the receiver. Note
the printed `code`, `bit length`, `pulse length`, `protocol`. Confirm the
value repeats identically across presses (if it changes every press, it's
a rolling code and this approach won't work).

You can unwire the receiver after this step if you like — it's not needed
at runtime.

## 3. Configure the code values

Set them as environment variables (matches the systemd service file), or
edit the defaults directly in [vision/config.py](vision/config.py):

```
export CODE_VALUE=5592371
export CODE_BIT_LENGTH=24
export CODE_PULSE_LENGTH=350
export CODE_PROTOCOL=1
```

## 4. Test the transmitter

Wire the 433MHz transmitter (GPIO17), then quickly sanity check it fires:

```
python3 -c "import rf_control; rf_control.send_code()"
```

Your light should toggle.

## 5. Run the gesture detector

```
python3 gesture_trigger.py
```

Raise your hand and hold it for ~1s — you should see "Trigger sent" in the
terminal and the light should toggle.

Mount the camera + IR illuminator somewhere overviewing the room, then run
it permanently as a service:

```
cp gesture-trigger.service.example /etc/systemd/system/gesture-trigger.service
# edit the CODE_* values and paths in that file first
sudo systemctl daemon-reload
sudo systemctl enable --now gesture-trigger
```

## Tuning (vision/config.py)

- `HOLD_SECONDS` — how long the hand must stay raised before it counts. Raise this if incidental arm movement ever triggers it.
- `COOLDOWN_SECONDS` — minimum time between triggers, so one gesture can't double-toggle the light.
- `WRIST_ABOVE_SHOULDER_MARGIN` — how far above the shoulder the wrist must be, in normalized frame coordinates.
- `TX_REPEAT` — how many times the code is repeated per send; raise if the light misses triggers, lower if you notice lag.
