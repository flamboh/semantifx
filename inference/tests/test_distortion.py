import unittest

import torch

from semantifx_inference.distortion import process


class DistortionTests(unittest.TestCase):
    def test_zero_mix_is_exactly_dry(self):
        audio = torch.linspace(-1.0, 1.0, 257).reshape(1, 1, -1)
        output = process(
            audio,
            48_000,
            drive_db=torch.tensor([36.0]),
            character=torch.tensor([1.0]),
            tone_db=torch.tensor([12.0]),
            mix=torch.tensor([0.0]),
        )
        torch.testing.assert_close(output, audio, rtol=0.0, atol=0.0)

    def test_drive_has_a_finite_gradient(self):
        audio = torch.sin(torch.linspace(0.0, 20.0, 2048)).reshape(1, 1, -1)
        drive = torch.tensor([18.0], requires_grad=True)
        output = process(
            audio,
            48_000,
            drive_db=drive,
            character=torch.tensor([0.5]),
            tone_db=torch.tensor([0.0]),
            mix=torch.tensor([1.0]),
        )
        output.square().mean().backward()
        self.assertIsNotNone(drive.grad)
        self.assertTrue(torch.isfinite(drive.grad).all())
        self.assertGreater(abs(float(drive.grad.item())), 0.0)

    def test_character_changes_the_curve_without_changing_endpoints(self):
        audio = torch.tensor([[[-1.0, -0.25, 0.25, 1.0]]])
        common = {
            "drive_db": torch.tensor([36.0]),
            "tone_db": torch.tensor([0.0]),
            "mix": torch.tensor([1.0]),
        }
        soft = process(audio, 48_000, character=torch.tensor([0.0]), **common)
        hard = process(audio, 48_000, character=torch.tensor([1.0]), **common)
        torch.testing.assert_close(soft[..., [0, -1]], audio[..., [0, -1]], atol=1.0e-6, rtol=1.0e-6)
        torch.testing.assert_close(hard[..., [0, -1]], audio[..., [0, -1]], atol=1.0e-6, rtol=1.0e-6)
        self.assertFalse(torch.allclose(soft, hard))


if __name__ == "__main__":
    unittest.main()
