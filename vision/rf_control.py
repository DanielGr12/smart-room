"""Thin wrapper around rpi-rf for sending/receiving 433MHz codes directly
from Raspberry Pi GPIO pins (no separate microcontroller needed).
"""

from rpi_rf import RFDevice

import config


def send_code():
    rfdevice = RFDevice(config.RF_TX_GPIO)
    rfdevice.enable_tx()
    rfdevice.tx_repeat = config.TX_REPEAT
    rfdevice.tx_code(
        config.CODE_VALUE,
        config.CODE_PROTOCOL,
        config.CODE_PULSE_LENGTH,
        config.CODE_BIT_LENGTH,
    )
    rfdevice.cleanup()
