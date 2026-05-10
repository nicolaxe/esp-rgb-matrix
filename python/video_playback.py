import cv2
from rgb_array import RgbArray
from pathlib import Path
import sys
from numpy import ndarray, average
import time
from math import sqrt, log


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
#print(frame_time)


def srgb2lin(x: float) -> float:
    if x <= 0.04045:
        return x / 12.92
    else:
        return ((x + 0.055) / 1.055) ** 2.4
    
# def convf(x: float) -> float:
#     return x / (x + 0.5)

def conv(x: float, avg: float) -> int:
    y = srgb2lin(float(x) / 255) ** (0.75 *avg + 0.5)

    return min(255, int(y * 127))

    #return int(1.021968271 ** x)

    #return int(convf(x /255) * 255)

# def conv(v):
#     v = int(v)
#     x = v / 255.0

#     # Strong highlight compression
#     x = x * x

#     # Lift the darkest visible values slightly
#     if x > 0:
#         x = 0.02 + x * 0.98

#     return int(x * 255)

    

class FakeImage:
    def __init__(self, frame: ndarray) -> None:
        self.frame = frame
        self.avg = average(frame) / 255

    def getpixel(self, pos: tuple[int, int]) -> tuple[int, int, int]:
        b, g, r = self.frame[pos[1]][pos[0]]
        print(self.avg)

        
        r = conv(r, self.avg)
        g = conv(g, self.avg)
        b = conv(b, self.avg)
        
        return (r, g, b)



while cap.isOpened():
    start = time.perf_counter()
    status, frame = cap.read()
 
    # if frame is read correctly ret is True
    if not status:
        print("Can't receive frame (stream end?). Exiting ...")
        break
    
    frame = cv2.resize(frame, (32, 32))
    #frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    image = FakeImage(frame)
    rgb.send_image_udp(image)
    cv2.imshow('frame', cv2.resize(frame, (256, 256)))
    
    if cv2.waitKey(1) == ord('q'):
        break

    end = time.perf_counter()
    diff = end - start
    #print(diff)
    if diff > frame_time:
        print('Can\'t keep up. Frame took longer then frametime')
    else:
        time.sleep(frame_time - diff)

cap.release()
cv2.destroyAllWindows()