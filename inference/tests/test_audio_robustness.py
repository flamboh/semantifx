import unittest

import torch

from semantifx_inference.catalogue import CATALOGUE


class AudioRobustnessTests(unittest.TestCase):
    def test_non_finite_samples_render_as_silence_without_poisoning_other_samples(self):
        for effect_type, definition in CATALOGUE.items():
            for channels in (1, 2):
                with self.subTest(effect_type=effect_type, channels=channels):
                    audio = torch.linspace(-0.25, 0.25, 2048).repeat(1, channels, 1)
                    audio[0, 0, 255] = float("nan")
                    audio[0, 0, 512] = float("inf")
                    audio[0, 0, 768] = -float("inf")
                    clean = audio.clone()
                    clean[0, 0, [255, 512, 768]] = 0.0
                    params = {spec.name: torch.tensor([spec.default]) for spec in definition.specs}
                    actual = definition.render(audio, 44_100, params)
                    expected = definition.render(clean, 44_100, params)
                    self.assertTrue(torch.isfinite(actual).all())
                    torch.testing.assert_close(actual, expected, rtol=0.0, atol=0.0)
                    self.assertTrue(torch.isnan(audio[0, 0, 255]))
