"""Persistent JSON-lines matcher. Frames are points; templates are 2x crops."""
import json
import math
from pathlib import Path
import re
import sys
import time

import cv2
import numpy as np

cv2.setNumThreads(2)


class Matcher:
    def __init__(self, template_root):
        self.root = Path(template_root)
        self.templates = {}

    def match(self, request):
        started = time.perf_counter()
        try:
            frame = request['frame']
            if not all(isinstance(frame[key], (int, float)) and not isinstance(frame[key], bool)
                       and math.isfinite(frame[key]) for key in ('x', 'y', 'w', 'h')):
                raise ValueError('frame must contain finite point coordinates')
            if not 1 <= frame['w'] <= 8192 or not 1 <= frame['h'] <= 8192:
                raise ValueError('invalid frame size')
            name = request['name']
            if not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9_-]+', name):
                raise ValueError('invalid template name')
            path = self.root / (name + '.png')
            stat = path.stat()
            signature = (stat.st_mtime_ns, stat.st_size, stat.st_ino)
            cached = self.templates.get(name)
            if cached is None or cached[0] != signature:
                template = cv2.imread(str(path))
                if template is None:
                    raise ValueError('unreadable template')
                # CCOEFF_NORMED produces misleading perfect scores for constants.
                if float(np.max(cv2.meanStdDev(template)[1])) < 1e-6:
                    raise ValueError('flat template')
                self.templates[name] = (signature, template)
            else:
                template = cached[1]
            image_path = Path(request['image'])
            if not image_path.is_file():
                raise ValueError('missing image')
            image = cv2.imread(str(image_path))
            if image is None:
                raise ValueError('unreadable image')
            image = cv2.resize(image, (round(frame['w'] * 2), round(frame['h'] * 2)),
                               interpolation=cv2.INTER_AREA)
            height, width = template.shape[:2]
            if height > image.shape[0] or width > image.shape[1]:
                raise ValueError('template larger than normalized image')
            scores = cv2.matchTemplate(image, template, cv2.TM_CCOEFF_NORMED)
            np.nan_to_num(scores, copy=False, nan=-1, posinf=-1, neginf=-1)
            _, score, _, (x, y) = cv2.minMaxLoc(scores)
            scores[max(0, y-height//2):y+height//2+1,
                   max(0, x-width//2):x+width//2+1] = -1
            second = float(scores.max())
            found = score >= .97 and second < .95
            # Fixed offset calibrated from the supplied System 7 menu capture;
            # its template excludes the hover-dependent final Shut Down row.
            center_x = width / 2
            center_y = 235 if name == 'menu' else height / 2
            # App-specific title crop includes the go-away box at this hotspot.
            if name == 'cannedbsd-close':
                center_x, center_y = 28, 18
            click_x = frame['x'] + (x + center_x) / 2
            click_y = frame['y'] + (y + center_y) / 2
            if not (frame['x'] <= click_x < frame['x'] + frame['w']
                    and frame['y'] <= click_y < frame['y'] + frame['h']):
                raise ValueError('target outside window frame')
            response = {'found': found, 'score': score, 'second': second,
                        'x': click_x, 'y': click_y, 'frame': frame}
        except (KeyError, TypeError, ValueError, OSError, cv2.error) as error:
            response = {'found': False, 'error': str(error)}
        response['ms'] = (time.perf_counter() - started) * 1000
        return response


def main():
    matcher = Matcher(Path(__file__).parent / 'templates')
    if '--ready' in sys.argv[1:]:
        print(json.dumps({'ready': True, 'protocol': 1}), flush=True)
    for line in sys.stdin:
        try:
            response = matcher.match(json.loads(line))
        except (ValueError, TypeError) as error:
            response = {'found': False, 'error': str(error)}
        print(json.dumps(response, allow_nan=False), flush=True)


if __name__ == '__main__':
    main()
