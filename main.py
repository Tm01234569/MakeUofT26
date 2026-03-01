import cv2
import mediapipe as mp
from dataclasses import dataclass

@dataclass
class TrackResult:
    found: bool
    bbox: tuple | None          # (x, y, w, h) in pixels
    center: tuple | None        # (cx, cy) in pixels
    center_smooth: tuple | None # (cx_s, cy_s) in pixels
    confidence: float | None

class FaceTracker:
    """
    Face tracking component:
    - Detects a face every frame using MediaPipe
    - Returns bbox + center
    - Applies exponential smoothing to reduce jitter
    """
    def __init__(self, smooth_alpha: float = 0.25, min_confidence: float = 0.6):
        self.alpha = float(smooth_alpha)
        self.prev_cx = None
        self.prev_cy = None

        self.mp_face = mp.solutions.face_detection
        self.detector = self.mp_face.FaceDetection(
            model_selection=0,  # 0 = short range, faster; 1 = longer range
            min_detection_confidence=min_confidence
        )

    def update(self, frame_bgr) -> TrackResult:
        h, w = frame_bgr.shape[:2]
        frame_rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)

        out = self.detector.process(frame_rgb)
        if not out.detections:
            return TrackResult(False, None, None, None, None)

        # pick the highest-confidence detection
        best = max(out.detections, key=lambda d: d.score[0])
        conf = float(best.score[0])

        rel = best.location_data.relative_bounding_box
        x = int(rel.xmin * w)
        y = int(rel.ymin * h)
        bw = int(rel.width * w)
        bh = int(rel.height * h)

        # clamp to frame
        x = max(0, min(x, w - 1))
        y = max(0, min(y, h - 1))
        bw = max(1, min(bw, w - x))
        bh = max(1, min(bh, h - y))

        cx = x + bw // 2
        cy = y + bh // 2

        # exponential smoothing
        if self.prev_cx is None:
            cx_s, cy_s = cx, cy
        else:
            cx_s = int(self.alpha * cx + (1 - self.alpha) * self.prev_cx)
            cy_s = int(self.alpha * cy + (1 - self.alpha) * self.prev_cy)

        self.prev_cx, self.prev_cy = cx_s, cy_s

        return TrackResult(True, (x, y, bw, bh), (cx, cy), (cx_s, cy_s), conf)

def main():
    cap = cv2.VideoCapture(0)
    if not cap.isOpened():
        raise RuntimeError("Could not open webcam. Try VideoCapture(1) or (2).")

    tracker = FaceTracker(smooth_alpha=0.25, min_confidence=0.6)

    while True:
        ok, frame = cap.read()
        if not ok:
            break

        res = tracker.update(frame)

        if res.found:
            x, y, bw, bh = res.bbox
            cx, cy = res.center
            cxs, cys = res.center_smooth

            # draw bbox
            cv2.rectangle(frame, (x, y), (x + bw, y + bh), (0, 255, 0), 2)

            # draw raw + smooth centers
            cv2.circle(frame, (cx, cy), 4, (255, 0, 0), -1)      # raw (blue)
            cv2.circle(frame, (cxs, cys), 6, (0, 0, 255), -1)    # smooth (red)

            cv2.putText(frame, f"conf={res.confidence:.2f}", (x, y - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

            # This is the "tracking output" you’d use in your project:
            # print(f"Face center smooth: ({cxs}, {cys})")

        cv2.imshow("Face Tracking", frame)
        if cv2.waitKey(1) & 0xFF == ord("q"):
            break

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()