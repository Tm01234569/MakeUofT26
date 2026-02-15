import time
import threading
import numpy as np
import cv2
import serial
import os
import requests
import mediapipe as mp
from dataclasses import dataclass
from mediapipe.tasks import python as mp_tasks
from mediapipe.tasks.python import vision

# =========================
# CONFIG
# =========================
SERIAL_PORT = "/dev/cu.SLAB_USBtoUART"   # <- CHANGE to your ESP32 port
BAUD = 1500000                           # <- must match your ESP32 camera stream baud
W, H = 160, 120                          # <- must match your ESP32 image size

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_short_range/float16/latest/blaze_face_short_range.tflite"
MODEL_PATH = "blaze_face_short_range.tflite"

# =========================
# Camera stream receiver
# =========================
class SerialCamReceiver:
    """
    Reads grayscale frames sent from ESP32.
    Expected protocol (your code):
      - lines containing START_IMAGE
      - then raw bytes (coming in via readline chunks)
      - line containing END_IMAGE
    Writes latest frame into self.frame (H x W uint8).
    """
    def __init__(self, ser: serial.Serial, w: int, h: int, launch_cv2=False):
        self.ser = ser
        self.w = w
        self.h = h
        self.frame = np.full((h, w), 255, dtype=np.uint8)
        self._buf = np.full((h, w), 255, dtype=np.uint8)
        self.lock = threading.Lock()
        self.running = True
        self.launch_cv2 = launch_cv2

        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

        if launch_cv2:
            threading.Thread(target=self._preview, daemon=True).start()

    def _preview(self):
        while self.running:
            with self.lock:
                img = self.frame.copy()
            cv2.imshow("Serial Camera (Grayscale)", img)
            cv2.waitKey(1)

    def _run(self):
        x = 0
        y = 0
        state = 0  # 0 idle, 1 writing

        self.ser.reset_input_buffer()

        while self.running:
            datas = self.ser.readline()
            if len(datas) == 0:
                continue

            # ignore your other messages
            if b"Start!" in datas:
                state = 0
                x = y = 0
                continue

            # If we are idle, wait for frame start
            if state == 0:
                if b"START_IMAGE" in datas:
                    state = 1
                    x = y = 0
                continue

            # If writing, check end
            if b"END_IMAGE" in datas:
                # publish frame (IMPORTANT: write into shared array)
                with self.lock:
                    self.frame[:] = self._buf
                state = 0
                x = y = 0
                self.ser.reset_input_buffer()
                continue

            # Otherwise treat datas as pixel bytes
            for b in datas:
                self._buf[y, x] = b
                x += 1
                if x >= self.w:
                    x = 0
                    y += 1
                if y >= self.h:
                    # wrap if overflow
                    y = 0

    def get_bgr(self):
        with self.lock:
            gray = self.frame.copy()
        bgr = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)
        return bgr

    def stop(self):
        self.running = False


# =========================
# Face tracker (MediaPipe Tasks)
# =========================
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


# =========================
# MAIN (integrated)
# =========================
def main():
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.05)
    time.sleep(2.0)
    print("[SERIAL] Connected:", ser.port)

    cam = SerialCamReceiver(ser, W, H, launch_cv2=False)
    tracker = FaceTrackerTasks(smooth_alpha=0.25)

    pan = 90.0
    Kp = 18.0
    max_step = 3.0
    deadband_px = 12  # for 160px width, smaller than laptop; tune 8–20
    last_sent = None

    while True:
        frame = cam.get_bgr()
        h, w = frame.shape[:2]
        cx0 = w // 2

        res = tracker.update(frame)

        cv2.drawMarker(frame, (cx0, h // 2), (255, 255, 255),
                       markerType=cv2.MARKER_CROSS, markerSize=12, thickness=2)

        if res.found:
            x, y, bw, bh = res.bbox
            cxs, cys = res.center_smooth
            cv2.rectangle(frame, (x, y), (x + bw, y + bh), (0, 255, 0), 2)
            cv2.circle(frame, (cxs, cys), 4, (0, 0, 255), -1)

            ex = cxs - cx0
            if abs(ex) > deadband_px:
                nx = clamp(ex / (w / 2), -1.0, 1.0)
                step = clamp(Kp * nx, -max_step, max_step)
                pan += step
            pan = clamp(pan, 0.0, 180.0)

        angle = int(round(pan))
        if angle != last_sent:
            # IMPORTANT: send something the ESP32 can parse without breaking your image protocol
            ser.write(f"ANG:{angle}\n".encode("utf-8"))
            last_sent = angle

        cv2.putText(frame, f"PAN={pan:5.1f}", (5, 18),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.55, (255, 255, 255), 2)

        cv2.imshow("SerialCam -> Face -> Servo", frame)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cam.stop()
    ser.close()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
