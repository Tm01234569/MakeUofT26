import cv2
import os
import requests
import mediapipe as mp
import serial
import time
from dataclasses import dataclass
from mediapipe.tasks import python as mp_tasks
from mediapipe.tasks.python import vision

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_short_range/float16/latest/blaze_face_short_range.tflite"
MODEL_PATH = "blaze_face_short_range.tflite"

# CHANGE THIS to your ESP32 serial port (macOS example below)
SERIAL_PORT = "/dev/cu.usbserial-0001"
BAUD = 115200


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

        # Clamp bbox inside frame
        x = max(0, min(x, w - 1))
        y = max(0, min(y, h - 1))
        bw = max(1, min(bw, w - x))
        bh = max(1, min(bh, h - y))

        cx, cy = x + bw // 2, y + bh // 2

        # Exponential smoothing
        if self.prev is None:
            cxs, cys = cx, cy
        else:
            cxs = int(self.alpha * cx + (1 - self.alpha) * self.prev[0])
            cys = int(self.alpha * cy + (1 - self.alpha) * self.prev[1])

        self.prev = (cxs, cys)
        return TrackResult(True, (x, y, bw, bh), (cxs, cys))


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def open_camera_avfoundation(max_index=3):
    """
    Try to open a camera on macOS using the AVFoundation backend.
    Attempts indices 0..max_index and returns an opened VideoCapture.
    """
    for idx in range(0, max_index + 1):
        cap = cv2.VideoCapture(idx, cv2.CAP_AVFOUNDATION)
        if cap.isOpened():
            print(f"[CAM] Opened camera index {idx}")
            return cap
    return None


def main():
    # --- serial connect ---
    # Strip prevents the "leading space in path" bug from ever happening again.
    port = SERIAL_PORT.strip()

    try:
        ser = serial.Serial(port, BAUD, timeout=0.01)
        time.sleep(2.0)  # allow ESP32 reset on connect
        print(f"[SERIAL] Connected: {ser.port} @ {BAUD}")
    except Exception as e:
        ser = None
        print(f"[SERIAL] WARNING: could not open {port!r}: {e}")
        print("[SERIAL] Continuing WITHOUT servo output...")

    # --- camera open ---
    cap = open_camera_avfoundation(max_index=3)
    if cap is None:
        raise RuntimeError("Could not open webcam (tried indices 0-3).")

    tracker = FaceTrackerTasks(smooth_alpha=0.25)

    # --- control params ---
    pan = 90.0
    Kp = 18.0
    max_step = 3.0
    deadband_px = 40
    last_sent = None

    print("[INFO] Running. Press 'q' to quit.")

    while True:
        ok, frame = cap.read()
        if not ok or frame is None:
            print("[CAM] Frame grab failed, retrying...")
            time.sleep(0.05)
            continue

        h, w = frame.shape[:2]
        cx0 = w // 2

        res = tracker.update(frame)

        # draw center crosshair
        cv2.drawMarker(
            frame,
            (cx0, h // 2),
            (255, 255, 255),
            markerType=cv2.MARKER_CROSS,
            markerSize=20,
            thickness=2,
        )

        if res.found:
            x, y, bw, bh = res.bbox
            cxs, cys = res.center_smooth

            cv2.rectangle(frame, (x, y), (x + bw, y + bh), (0, 255, 0), 2)
            cv2.circle(frame, (cxs, cys), 6, (0, 0, 255), -1)

            # error relative to center
            ex = cxs - cx0

            if abs(ex) > deadband_px:
                nx = clamp(ex / (w / 2), -1.0, 1.0)
                step = clamp(Kp * nx, -max_step, max_step)
                pan += step

            pan = clamp(pan, 0.0, 180.0)

        # send integer angle occasionally (avoid spam)
        angle = int(round(pan))
        if ser is not None and angle != last_sent:
            ser.write(f"{angle}\n".encode("utf-8"))
            last_sent = angle

        cv2.putText(
            frame,
            f"PAN={pan:5.1f} deg",
            (10, 30),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.8,
            (255, 255, 255),
            2,
        )

        cv2.imshow("Face -> ESP32 Servo PAN", frame)
        key = cv2.waitKey(1) & 0xFF
        if key == ord("q"):
            break

    cap.release()
    if ser is not None:
        ser.close()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
