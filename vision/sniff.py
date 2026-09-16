"""Run this first to capture your light remote's 433MHz code.

    python3 sniff.py

Press your real remote's ON button several times near the receiver.
Note the printed code, pulse length, protocol, and bit length -- you'll
need all four in config.py. Confirm the value repeats identically across
presses (if it changes every press, it's a rolling code and this whole
approach won't work).
"""

import signal
import time

from rpi_rf import RFDevice

import config

rfdevice = RFDevice(config.RF_RX_GPIO)
rfdevice.enable_rx()

print(f"Listening on GPIO{config.RF_RX_GPIO}. Press your remote's button now...")

timestamp = None
try:
    while True:
        if rfdevice.rx_code_timestamp != timestamp:
            timestamp = rfdevice.rx_code_timestamp
            print("---- Signal received ----")
            print(f"code:          {rfdevice.rx_code}")
            print(f"bit length:    {rfdevice.rx_bitlength}")
            print(f"pulse length:  {rfdevice.rx_pulselength}")
            print(f"protocol:      {rfdevice.rx_proto}")
        time.sleep(0.01)
except KeyboardInterrupt:
    pass
finally:
    rfdevice.cleanup()
