import time
import os
import requests
import numpy as np
import cv2
import serial
import mediapipe as mp
from dataclasses import dataclass
from mediapipe.tasks import python as mp_tasks
from mediapipe.tasks.python import vision

# ================= CONFIG =================
SERIAL_PORT = "/dev/cu.SLAB_USBtoUART"  # <-- CHANGE to your Mac ESP32 port
BAUD = 1500000

W, H = 160, 120
FRAME_BYTES = W * H

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_short_range/float16/latest/blaze_face_short_range.tflite"
MODEL_PATH = "blaze_face_short_range.tflite"


@dataclass
class TrackResult:
    found: bool
    bbox: tuple | None
    center_smooth: tuple | None


class FaceTrackerTasks:
    def __init__(self, smooth_alpha=0.25):
        self.alpha = float(smooth_alpha)
        self.prev = None

        if not os.path.exists(MODEL_PATH):
            print("[MODEL] Downloading face model...")
            r = requests.get(MODEL_URL, timeout=60)
            r.raise_for_status()
            with open(MODEL_PATH, "wb") as f:
                f.write(r.content)
            print("[MODEL] Saved:", MODEL_PATH)

        base_options = mp_tasks.BaseOptions(model_asset_path=MODEL_PATH)
        options = vision.FaceDetectorOptions(base_options=base_options)
        self.detector = vision.FaceDetector.create_from_options(options)

    def update(self, frame_bgr) -> TrackResult:
        h, w = frame_bgr.shape[:2]
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
        mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=frame_rgb)

        result = self.detector.detect(mp_image)
        if not result.detections:
            self.prev = None
            return TrackResult(False, None, None)

        det = result.detections[0]
        bb = det.bounding_box
        x, y, bw, bh = int(bb.origin_x), int(bb.origin_y), int(bb.width), int(bb.height)

        x = max(0, min(x, w - 1))
        y = max(0, min(y, h - 1))
        bw = max(1, min(bw, w - x))
        bh = max(1, min(bh, h - y))

        cx, cy = x + bw // 2, y + bh // 2

        if self.prev is None:
            cxs, cys = cx, cy
        else:
            cxs = int(self.alpha * cx + (1 - self.alpha) * self.prev[0])
            cys = int(self.alpha * cy + (1 - self.alpha) * self.prev[1])

        self.prev = (cxs, cys)
        return TrackResult(True, (x, y, bw, bh), (cxs, cys))


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def read_frame_exact(ser: serial.Serial):
    # Sync to START_IMAGE marker
    while True:
        line = ser.readline()
        if b"START_IMAGE" in line:
            break

    # Read exact raw frame bytes (19200 for 160x120)
    buf = ser.read(FRAME_BYTES)
    if len(buf) != FRAME_BYTES:
        return None

    # Consume until END_IMAGE marker
    while True:
        line = ser.readline()
        if b"END_IMAGE" in line:
            break

    frame_gray = np.frombuffer(buf, dtype=np.uint8).reshape((H, W))
    return frame_gray


def main():
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()
    print(f"[SERIAL] Connected: {SERIAL_PORT} @ {BAUD}")

    tracker = FaceTrackerTasks(smooth_alpha=0.25)

    # ===== Control tuning for 160x120 =====
    pan = 90.0
    Kp = 22.0
    max_step = 3.0
    deadband_px = 10
    last_sent = None

    print("[INFO] Running. Press 'q' to quit.")

    while True:
        gray = read_frame_exact(ser)
        if gray is None:
            continue

        frame = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        h, w = frame.shape[:2]
        cx0, cy0 = w // 2, h // 2

        res = tracker.update(frame)

        # draw center crosshair
        cv2.drawMarker(frame, (cx0, cy0), (255, 255, 255),
                       markerType=cv2.MARKER_CROSS, markerSize=12, thickness=2)

        if res.found:
            x, y, bw, bh = res.bbox
            cxs, cys = res.center_smooth

            cv2.rectangle(frame, (x, y), (x + bw, y + bh), (0, 255, 0), 2)
            cv2.circle(frame, (cxs, cys), 4, (0, 0, 255), -1)

            ex = cxs - cx0  # + means face right of center
            if abs(ex) > deadband_px:
                nx = clamp(ex / (w / 2), -1.0, 1.0)
                step = clamp(Kp * nx, -max_step, max_step)

                # If direction is reversed, change to: pan -= step
                pan += step

            pan = clamp(pan, 0.0, 180.0)

        angle = int(round(pan))
        if angle != last_sent:
            ser.write(f"ANG:{angle}\n".encode("utf-8"))
            last_sent = angle

        cv2.putText(frame, f"PAN={pan:5.1f} deg", (5, 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2)

        cv2.imshow("ESP32 Cam -> Face -> Servo", frame)
        if (cv2.waitKey(1) & 0xFF) == ord('q'):
            break

    ser.close()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
