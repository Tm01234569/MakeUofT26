import serial
import time
from PIL import Image
import cv2
import numpy as np
import threading
import copy

def cam_stream(COM, image_out: np.ndarray, launch_cv2 = False):
    h, w = image_out.shape
    print(w,h)
    im = Image.new("L",(w, h))
    cv2im_buffer = np.full((h, w),255,dtype=np.uint8)

    def show_cv2():
        print("cv2 time!")
        while True:
            cv_image_bgr = cv2.cvtColor(image_out, cv2.COLOR_GRAY2BGR)
            cv2.imshow("Video Stream", cv_image_bgr)
            cv2.waitKey(1)

    if launch_cv2:
        cv2_thread = threading.Thread(target=show_cv2)
        cv2_thread.start()

    port = serial.Serial(COM,1500000)
    time.sleep(2)
    x = 0
    y = 0
    maxy = 0

    port.reset_input_buffer()
    state = 0 #0 = idle, 1 = writing
    while True:
        datas = port.readline()
        if "Start!".encode('utf-8') in datas:
            print("resetting")
            state = 0
        if len(datas) == 0:
            continue
        match(state):
            case 0:
                if "START_IMAGE".encode('utf-8') in datas:
                    print("found start")
                    state = 1
                continue
            case 1:
                if "END_IMAGE".encode('utf8') in datas:
                    print("found end")
                    state = 0
                    x = 0
                    y = 0
                    image_out = copy.copy(cv2im_buffer)
                    port.reset_input_buffer()
                else:
                    for data in datas:
                        cv2im_buffer[y,x] = data
                        #print((x,y))
                        x += 1
                        maxy = max(maxy, y)
                        if x >= w:
                            x = 0
                            y += 1
                        if y >= h:
                            y = 0
                continue

def exmaple():
    cam_stream('COM8',np.full((120, 160),255,dtype=np.uint8), True)
