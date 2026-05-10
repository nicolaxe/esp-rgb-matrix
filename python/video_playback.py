import cv2
import sys
import time
import subprocess

from numpy import ndarray, average
from rgb_array import RgbArray
from pathlib import Path


rgb = RgbArray('192.168.1.134')

if len(sys.argv) != 2:
    print('Incorrect arguments')
    exit()

if not Path(sys.argv[1]).exists():
    print('File not found')
    exit()

cap = cv2.VideoCapture(sys.argv[1])
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 32)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 32)
frame_time = 1/cap.get(cv2.CAP_PROP_FPS)


def srgb2lin(x: float) -> float:
    if x <= 0.04045:
        return x / 12.92
    else:
        return ((x + 0.055) / 1.055) ** 2.4
    

def conv(x: float, avg: float) -> int:
    y = srgb2lin(float(x) / 255) ** (0.75 *avg + 0.5)

    return min(255, int(y * 127))


class FakeImage:
    def __init__(self, frame: ndarray) -> None:
        self.frame = frame
        self.avg = average(frame) / 255

    def getpixel(self, pos: tuple[int, int]) -> tuple[int, int, int]:
        b, g, r = self.frame[pos[1]][pos[0]]
        #print(self.avg)

        
        r = conv(r, self.avg)
        g = conv(g, self.avg)
        b = conv(b, self.avg)
        
        return (r, g, b)

mpv_process = subprocess.Popen(['mpv', '--no-video', sys.argv[1]], 
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL,
                                stdin=subprocess.DEVNULL,
                                start_new_session=True)

try:
    while cap.isOpened():
        start = time.perf_counter()
        status, frame = cap.read()
    
        if not status:
            print("Can't receive frame (stream end?). Exiting ...")
            break
        
        frame = cv2.resize(frame, (32, 32))
        image = FakeImage(frame)
        rgb.send_image_udp(image)

        end = time.perf_counter()
        diff = end - start

        if diff > frame_time:
            print('Can\'t keep up. Frame took longer then frametime')
        else:
            time.sleep(frame_time - diff)
finally:
    mpv_process.terminate()
    cap.release()
    cv2.destroyAllWindows()
    