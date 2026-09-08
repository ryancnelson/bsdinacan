#!/usr/bin/env python3
"""Synthetic crops verify the matcher without controlling any desktop."""
import importlib.util
import json
import selectors
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import cv2
import numpy as np

sys.dont_write_bytecode = True
SCRIPT = Path(__file__).resolve().parents[1] / 'platform/mac68k/automation/match.py'
spec = importlib.util.spec_from_file_location('mac_match', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        random = np.random.default_rng(1234)
        self.template = random.integers(0, 256, (24, 32, 3), dtype=np.uint8)
        self.image = random.integers(0, 256, (180, 280, 3), dtype=np.uint8)
        self.path = self.root / 'image.png'
        cv2.imwrite(str(self.root / 'button.png'), self.template)
        self.matcher = module.Matcher(self.root)

    def request(self, scale=1):
        image = np.repeat(np.repeat(self.image, scale, axis=0), scale, axis=1)
        cv2.imwrite(str(self.path), image)
        return {'image': str(self.path), 'name': 'button',
                'frame': {'x': 100, 'y': 50, 'w': 140, 'h': 90}}

    def test_unique_match_returns_screen_point_center(self):
        self.image[50:74, 80:112] = self.template
        result = self.matcher.match(self.request())
        self.assertTrue(result['found'])
        self.assertEqual((result['x'], result['y']), (148, 81))
        self.assertLess(result['second'], .95)

    def test_retina_snapshot_normalizes_before_matching(self):
        self.image[50:74, 80:112] = self.template
        result = self.matcher.match(self.request(scale=2))
        self.assertTrue(result['found'])
        self.assertEqual((result['x'], result['y']), (148, 81))

    def test_duplicate_target_is_ambiguous(self):
        self.image[20:44, 40:72] = self.template
        self.image[100:124, 160:192] = self.template
        result = self.matcher.match(self.request())
        self.assertFalse(result['found'])
        self.assertGreaterEqual(result['second'], .97)

    def test_below_threshold_does_not_match(self):
        self.assertFalse(self.matcher.match(self.request())['found'])

    def test_flat_template_fails_closed_and_cache_refreshes(self):
        self.image[50:74, 80:112] = self.template
        request = self.request()
        self.assertTrue(self.matcher.match(request)['found'])
        cv2.imwrite(str(self.root / 'button.png'), np.zeros_like(self.template))
        result = self.matcher.match(request)
        self.assertFalse(result['found'])
        self.assertIn('flat template', result['error'])

    def test_invalid_frame_and_missing_image_fail_closed(self):
        request = self.request()
        request['frame']['w'] = float('nan')
        self.assertFalse(self.matcher.match(request)['found'])
        request = self.request()
        request['image'] = str(self.root / 'missing.png')
        self.assertFalse(self.matcher.match(request)['found'])

    def test_oversized_template_fails_closed(self):
        request = self.request()
        request['frame']['w'] = 1
        self.assertFalse(self.matcher.match(request)['found'])

    def test_ready_arrives_without_stdin_or_eof(self):
        process = subprocess.Popen([sys.executable, '-u', str(SCRIPT), '--ready'],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                self.assertTrue(selector.select(10), 'matcher never became ready')
            self.assertEqual(json.loads(process.stdout.readline()),
                             {'ready': True, 'protocol': 1})
            output, _ = process.communicate('null\n', timeout=10)
            self.assertFalse(json.loads(output)['found'])
            self.assertEqual(process.returncode, 0)
        finally:
            if process.poll() is None:
                process.kill()
            process.communicate()

    def test_protocol_handles_multiple_lines_and_bad_input(self):
        # Missing images fail immediately; no private or live screenshots needed.
        requests = 'not json\n' + json.dumps({'name': 'trash', 'image': '/missing',
                    'frame': {'x': 0, 'y': 0, 'w': 500, 'h': 400}}) + '\nnull\n'
        result = subprocess.run([sys.executable, str(SCRIPT)], input=requests,
                                capture_output=True, text=True, timeout=10, check=True)
        responses = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(responses), 3)
        self.assertTrue(all(response['found'] is False for response in responses))
        self.assertTrue(all('error' in response for response in responses))


if __name__ == '__main__':
    unittest.main()
