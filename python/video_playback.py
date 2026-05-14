import cv2
import time
import subprocess
import argparse

import numpy as np
from rgb_array import RgbArray
from pathlib import Path

parser = argparse.ArgumentParser()

parser.add_argument('file', help='media file to play')
parser.add_argument('-a', '--audio', action='store_true', help='play audio locally (requires mpv)')
parser.add_argument('-l', '--local', action='store_true', help='play video locally (requires mpv)' )

args = parser.parse_args()

if not Path(args.file).exists():
    print('File not found')
    exit()
elif not Path(args.file).is_file():
    print('Not a file')
    exit()

cap = cv2.VideoCapture(args.file)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 32)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 32)
frame_time = 1/cap.get(cv2.CAP_PROP_FPS)

rgb = RgbArray('192.168.1.134')


def srgb2lin(x: float) -> float:
    if x <= 0.04045:
        return x / 12.92
    else:
        return ((x + 0.055) / 1.055) ** 2.4
    

def conv(x: float, avg: float) -> int:
    y = srgb2lin(float(x) / 255) ** (0.75 * avg + 0.5)

    return min(255, int(y * 127))


class FakeImage:
    def __init__(self, frame: np.ndarray) -> None:
        self.frame = frame
        self.avg = np.average(frame) / 255

    def getpixel(self, pos: tuple[int, int]) -> tuple[int, int, int]:
        b, g, r = self.frame[pos[1]][pos[0]]
        #print(self.avg)

        
        r = conv(r, self.avg)
        g = conv(g, self.avg)
        b = conv(b, self.avg)
        
        return (r, g, b)

try:
    if args.audio or args.local:
        params = ['mpv', args.file, '--osc=no', '--no-input-default-bindings', '--config=no']
        if not args.local:
            params.append('--no-video')
        if not args.audio:
            params.append('--no-audio')
        mpv_process = subprocess.Popen(params, 
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL,
                                stdin=subprocess.DEVNULL,
                                start_new_session=True)
    else:
        mpv_process = None
except:
    print('Failed to open mpv')
    mpv_process = None

try:
    while cap.isOpened():
        start = time.perf_counter()
        status, frame = cap.read()
    
        if not status:
            print("Can't receive frame (stream end?). Exiting ...")
            break
        
        frame = cv2.resize(frame, (32, 32), interpolation=cv2.INTER_AREA)
        image = FakeImage(frame)
        rgb.send_image_udp(image)

        end = time.perf_counter()
        diff = end - start

        if mpv_process:
            if mpv_process.poll() is not None:
                print('mpv exited. Exiting ...')
                exit()

        if diff > frame_time:
            print('Can\'t keep up. Frame took longer then frametime')
        else:
            time.sleep(frame_time - diff)
finally:
    if mpv_process:
        mpv_process.terminate()
    cap.release()
    cv2.destroyAllWindows()
    rgb.send_image_udp(FakeImage(np.zeros((32, 32, 3), dtype=np.uint8)))
    