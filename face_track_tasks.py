import cv2
import os
import requests
from dataclasses import dataclass

from mediapipe.tasks import python as mp_tasks
from mediapipe.tasks.python import vision

MODEL_URL = "https://storage.googleapis.com/mediapipe-models/face_detector/blaze_face_short_range/float16/latest/blaze_face_short_range.tflite"
MODEL_PATH = "blaze_face_short_range.tflite"

@dataclass
class TrackResult:
    found: bool
    bbox: tuple | None          # (x, y, w, h)
    center: tuple | None        # (cx, cy)
    center_smooth: tuple | None # (cx_s, cy_s)
    confidence: float | None

class FaceTrackerTasks:
    def __init__(self, smooth_alpha=0.25):
        self.alpha = smooth_alpha
        self.prev = None

        if not os.path.exists(MODEL_PATH):
            print("Downloading face model...")
            r = requests.get(MODEL_URL, timeout=60)
            r.raise_for_status()
            with open(MODEL_PATH, "wb") as f:
                f.write(r.content)

        base_options = mp_tasks.BaseOptions(model_asset_path=MODEL_PATH)
        options = vision.FaceDetectorOptions(base_options=base_options)
        self.detector = vision.FaceDetector.create_from_options(options)

    def update(self, frame_bgr) -> TrackResult:
        h, w = frame_bgr.shape[:2]
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)

        mp_image = mp_tasks.vision.Image(
            image_format=mp_tasks.vision.ImageFormat.SRGB,
            data=frame_rgb
        )

        detection_result = self.detector.detect(mp_image)

        if not detection_result.detections:
            return TrackResult(False, None, None, None, None)

        det = detection_result.detections[0]
        bbox = det.bounding_box  # origin_x, origin_y, width, height
        x, y, bw, bh = bbox.origin_x, bbox.origin_y, bbox.width, bbox.height

        cx, cy = x + bw // 2, y + bh // 2
        if self.prev is None:
            cxs, cys = cx, cy
        else:
            cxs = int(self.alpha * cx + (1 - self.alpha) * self.prev[0])
            cys = int(self.alpha * cy + (1 - self.alpha) * self.prev[1])
        self.prev = (cxs, cys)

        conf = det.categories[0].score if det.categories else None
        return TrackResult(True, (x, y, bw, bh), (cx, cy), (cxs, cys), conf)

def main():
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        raise RuntimeError("Could not open webcam. Try VideoCapture(1).")

    tracker = FaceTrackerTasks(smooth_alpha=0.25)

    while True:
        ok, frame = cap.read()
        if not ok:
            break

        res = tracker.update(frame)
        if res.found:
            x, y, bw, bh = res.bbox
            cxs, cys = res.center_smooth
            cv2.rectangle(frame, (x, y), (x + bw, y + bh), (0, 255, 0), 2)
            cv2.circle(frame, (cxs, cys), 6, (0, 0, 255), -1)
            if res.confidence is not None:
                cv2.putText(frame, f"conf={res.confidence:.2f}", (x, y - 10),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

        cv2.imshow("Face Tracking (MediaPipe Tasks)", frame)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()