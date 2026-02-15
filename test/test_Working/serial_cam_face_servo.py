import time, os, requests
import numpy as np
import cv2
import serial
import mediapipe as mp
from dataclasses import dataclass
from mediapipe.tasks import python as mp_tasks
from mediapipe.tasks.python import vision

# ===== CONFIG =====
SERIAL_PORT = "COM5"   # change
BAUD = 1500000

W, H = 160, 120
FRAME_BYTES = W * H

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_short_range/float16/latest/blaze_face_short_range.tflite"
MODEL_PATH = "blaze_face_short_range.tflite"

# ===== Smooth + speed knobs =====
SMOOTH_ALPHA = 0.12       # smaller = smoother (slower response)
Kp_x = 18.0               # lower = less twitchy
Kp_y = 18.0
MAX_STEP = 3.0            # degrees per update (like your original)
DEADBAND_PX = 8           # ignore tiny errors
SEND_EVERY_MS = 60        # only send servo command every 60ms (~16 Hz)

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
            print("[MODEL] downloading model...")
            r = requests.get(MODEL_URL, timeout=60)
            r.raise_for_status()
            with open(MODEL_PATH, "wb") as f:
                f.write(r.content)
            print("[MODEL] saved:", MODEL_PATH)

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

        # stronger smoothing
        if self.prev is None:
            cxs, cys = cx, cy
        else:
            cxs = int(self.alpha * cx + (1 - self.alpha) * self.prev[0])
            cys = int(self.alpha * cy + (1 - self.alpha) * self.prev[1])

        self.prev = (cxs, cys)
        return TrackResult(True, (x, y, bw, bh), (cxs, cys))

def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v

def read_frame_exact(ser):
    start_deadline = time.time() + 2.0
    while True:
        line = ser.readline()
        if b"START_IMAGE" in line:
            break
        if time.time() > start_deadline:
            ser.reset_input_buffer()
            return None

    buf = ser.read(FRAME_BYTES)
    if len(buf) != FRAME_BYTES:
        ser.reset_input_buffer()
        return None

    end_deadline = time.time() + 2.0
    while True:
        line = ser.readline()
        if b"END_IMAGE" in line:
            break
        if time.time() > end_deadline:
            ser.reset_input_buffer()
            return None

    return np.frombuffer(buf, dtype=np.uint8).reshape((H, W))

def step_from_err(err_px, half_size, Kp, max_step, deadband):
    if abs(err_px) <= deadband:
        return 0.0
    n = err_px / half_size
    return clamp(Kp * n, -max_step, max_step)

def main():
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()
    print(f"[SERIAL] connected {SERIAL_PORT} @ {BAUD}")

    tracker = FaceTrackerTasks(smooth_alpha=SMOOTH_ALPHA)

    pan = 90.0
    tilt = 90.0

    last_sent = None
    last_send_time = 0.0

    print("Running. Press q to quit.")

    while True:
        gray = read_frame_exact(ser)
        if gray is None:
            continue

        frame = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        h, w = frame.shape[:2]
        cx0, cy0 = w // 2, h // 2

        res = tracker.update(frame)

        cv2.drawMarker(frame, (cx0, cy0), (255, 255, 255),
                       markerType=cv2.MARKER_CROSS, markerSize=12, thickness=2)

        if res.found:
            x, y, bw, bh = res.bbox
            cxs, cys = res.center_smooth

            cv2.rectangle(frame, (x, y), (x + bw, y + bh), (0, 255, 0), 2)
            cv2.circle(frame, (cxs, cys), 4, (0, 0, 255), -1)

            ex = cxs - cx0
            ey = cys - cy0

            pan_step  = step_from_err(ex, w/2, Kp_x, MAX_STEP, DEADBAND_PX)
            tilt_step = step_from_err(ey, h/2, Kp_y, MAX_STEP, DEADBAND_PX)

            # flip sign if your mount is reversed:
            pan  = clamp(pan  + pan_step,  0.0, 180.0)
            tilt = clamp(tilt + tilt_step, 0.0, 180.0)

        p = int(round(pan))
        t = int(round(tilt))

        now = time.time()
        if (now - last_send_time) * 1000.0 >= SEND_EVERY_MS:
            msg = f"PT:{p},{t}\n"
            if msg != last_sent:
                ser.write(msg.encode("utf-8"))
                last_sent = msg
            last_send_time = now

        cv2.putText(frame, f"PAN={pan:5.1f}  TILT={tilt:5.1f}", (5, 16),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2)

        cv2.imshow("ESP32 Cam -> Face -> 2 Servos (Smooth)", frame)
        if (cv2.waitKey(1) & 0xFF) == ord('q'):
            break

    ser.close()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()