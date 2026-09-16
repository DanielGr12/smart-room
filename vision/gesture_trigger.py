"""Raised-hand gesture detector -> sends the 433MHz code directly from the Pi's GPIO.

Runs entirely locally (no cloud, no network hop). Watches for a wrist
landmark held above the shoulder landmark for HOLD_SECONDS, then transmits
the code captured by sniff.py. A cooldown after each trigger prevents
double-fires.

Usage:
    python3 gesture_trigger.py
"""

import time

import cv2
import mediapipe as mp

import config
import rf_control

mp_pose = mp.solutions.pose


def open_camera():
    if config.USE_PICAMERA:
        from picamera2 import Picamera2

        picam2 = Picamera2()
        cam_config = picam2.create_video_configuration(main={"size": (640, 480), "format": "RGB888"})
        picam2.configure(cam_config)
        picam2.start()

        def read():
            frame = picam2.capture_array()
            return True, frame

        return read
    else:
        cap = cv2.VideoCapture(0)
        return cap.read


def hand_raised(landmarks):
    left_wrist = landmarks[mp_pose.PoseLandmark.LEFT_WRIST]
    right_wrist = landmarks[mp_pose.PoseLandmark.RIGHT_WRIST]
    left_shoulder = landmarks[mp_pose.PoseLandmark.LEFT_SHOULDER]
    right_shoulder = landmarks[mp_pose.PoseLandmark.RIGHT_SHOULDER]

    margin = config.WRIST_ABOVE_SHOULDER_MARGIN

    left_up = left_wrist.visibility > 0.5 and left_wrist.y < left_shoulder.y - margin
    right_up = right_wrist.visibility > 0.5 and right_wrist.y < right_shoulder.y - margin

    return left_up or right_up


def send_trigger():
    try:
        rf_control.send_code()
        print("Trigger sent")
    except Exception as exc:
        print(f"Trigger failed: {exc}")


def main():
    read_frame = open_camera()

    gesture_start = None
    last_trigger = 0.0

    with mp_pose.Pose(
        model_complexity=1,
        min_detection_confidence=0.6,
        min_tracking_confidence=0.6,
    ) as pose:
        print("Gesture detector running. Ctrl+C to stop.")
        while True:
            ok, frame = read_frame()
            if not ok:
                time.sleep(0.05)
                continue

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB) if not config.USE_PICAMERA else frame
            results = pose.process(rgb)

            now = time.time()
            raised = bool(results.pose_landmarks) and hand_raised(results.pose_landmarks.landmark)

            if raised:
                if gesture_start is None:
                    gesture_start = now
                held_for = now - gesture_start

                if held_for >= config.HOLD_SECONDS and (now - last_trigger) >= config.COOLDOWN_SECONDS:
                    print("Raised hand held long enough -> triggering")
                    send_trigger()
                    last_trigger = now
                    gesture_start = None
            else:
                gesture_start = None

            time.sleep(0.03)  # ~30fps cap; plenty for a held-gesture check


if __name__ == "__main__":
    main()
