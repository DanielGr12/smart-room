import os

# --- 433MHz GPIO pins (BCM numbering) ---
RF_RX_GPIO = 27  # used only by sniff.py, can be unwired after capturing the code
RF_TX_GPIO = 17

# --- Fill these in from sniff.py's output ---
CODE_VALUE = int(os.environ.get("CODE_VALUE", "0"))  # e.g. 5592371
CODE_BIT_LENGTH = int(os.environ.get("CODE_BIT_LENGTH", "24"))
CODE_PULSE_LENGTH = int(os.environ.get("CODE_PULSE_LENGTH", "350"))  # microseconds
CODE_PROTOCOL = int(os.environ.get("CODE_PROTOCOL", "1"))
TX_REPEAT = 10  # how many times to repeat the code per send, improves reliability

# Camera source.
USE_PICAMERA = True

# Gesture tuning.
HOLD_SECONDS = 0.8          # how long the hand must stay raised to count
COOLDOWN_SECONDS = 2.5       # ignore new gestures for this long after a trigger
WRIST_ABOVE_SHOULDER_MARGIN = 0.03  # normalized-coordinate margin (0-1 range) to avoid edge-of-threshold flicker
