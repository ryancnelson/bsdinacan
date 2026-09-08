#!/usr/bin/env python3
"""Decoded-pixel validation, not a claim about Toolbox capture or sips."""
import importlib.util
from pathlib import Path
import sys
import unittest
import numpy as np

sys.dont_write_bytecode = True
source = Path(__file__).resolve().parents[1] / 'platform/mac68k/guest.py'
spec = importlib.util.spec_from_file_location('guest', source)
guest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guest)


class PixelTests(unittest.TestCase):
    def test_blank_white_black_and_tiny_noise_are_not_evidence(self):
        for value in [0, 255]:
            with self.assertRaisesRegex(guest.Rejection, 'blank'):
                guest.validate_pixels(np.full((384, 630), value, dtype=np.uint8), 630, 384)
        pixels = np.full((384, 630), 255, dtype=np.uint8)
        pixels[0, :10] = 0
        with self.assertRaisesRegex(guest.Rejection, 'blank'):
            guest.validate_pixels(pixels, 630, 384)

    def test_dimensions_must_match_and_contrast_is_required(self):
        pixels = np.full((384, 630), 255, dtype=np.uint8)
        pixels[10:20, 10:30] = 0
        guest.validate_pixels(pixels, 630, 384)
        with self.assertRaisesRegex(guest.Rejection, 'dimensions'):
            guest.validate_pixels(pixels, 400, 630)
        with self.assertRaisesRegex(guest.Rejection, 'dimensions'):
            guest.validate_pixels(None, 630, 384)


if __name__ == '__main__':
    unittest.main()
