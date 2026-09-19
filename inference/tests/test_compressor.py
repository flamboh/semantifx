import unittest

import dasp_pytorch
import torch

from semantifx_inference.catalogue import CATALOGUE, default_chain
from semantifx_inference.differentiable_chain import VariableChain
from semantifx_inference.effects.compressor import render


def burst_signal() -> torch.Tensor:
    audio = torch.full((1, 1, 4096), 0.02)
    audio[:, :, 1024:3072] = 1.0
    return audio


class _Signal:
    def __init__(self, audio_data: torch.Tensor) -> None:
        self.audio_data = audio_data

    def clone(self):
        return _Signal(self.audio_data.clone())


def compressor_parameters(requires_grad: bool = False, knee_db: float = 6.0):
    values = {
        "threshold_db": -20.0,
        "ratio": 4.0,
        "attack_ms": 20.0,
        "knee_db": knee_db,
        "makeup_gain_db": 0.0,
    }
    return {
        name: torch.tensor([value], requires_grad=requires_grad)
        for name, value in values.items()
    }


class CompressorTests(unittest.TestCase):
    def test_generated_ranges_are_pinned(self):
        chain = default_chain(["compressor"])
        self.assertEqual(
            [
                (name, (minimum, maximum), default)
                for name, minimum, maximum, default in [
                    (spec.name, spec.minimum, spec.maximum, spec.default)
                    for spec in CATALOGUE["compressor"].specs
                ]
            ],
            [
                ("threshold_db", (-60.0, 0.0), -20.0),
                ("ratio", (1.0, 20.0), 4.0),
                ("attack_ms", (5.0, 100.0), 20.0),
                ("knee_db", (1.0, 12.0), 6.0),
                ("makeup_gain_db", (0.0, 12.0), 0.0),
            ],
        )
        self.assertEqual(chain[0]["parameters"]["knee_db"], 6.0)

    def test_render_matches_dasp_compressor_with_fixed_release(self):
        sample_rate = 48_000
        audio = torch.randn(2, 2, 257)
        params = {
            "threshold_db": torch.tensor([-24.0, -18.0]),
            "ratio": torch.tensor([3.0, 8.0]),
            "attack_ms": torch.tensor([10.0, 40.0]),
            "knee_db": torch.tensor([2.0, 9.0]),
            "makeup_gain_db": torch.tensor([1.0, 4.0]),
        }
        actual = render(audio, sample_rate, params)
        expected = dasp_pytorch.functional.compressor(
            audio,
            sample_rate,
            release_ms=torch.full_like(params["attack_ms"], 50.0),
            **params,
        )
        self.assertEqual(actual.shape, audio.shape)
        self.assertTrue(torch.equal(actual, expected))

    def test_default_parameters_have_finite_gradients_and_dynamic_parameters_move(self):
        params = compressor_parameters(requires_grad=True)
        output = render(burst_signal(), 44_100, params)
        output.square().mean().backward()

        for name, parameter in params.items():
            self.assertIsNotNone(parameter.grad, name)
            self.assertTrue(torch.isfinite(parameter.grad).all(), name)
        for name in ("threshold_db", "ratio", "makeup_gain_db"):
            self.assertGreater(abs(float(params[name].grad.item())), 0.0, name)

    def test_knee_floor_has_finite_gradients(self):
        params = compressor_parameters(requires_grad=True, knee_db=1.0)
        render(burst_signal(), 44_100, params).square().mean().backward()

        for name, parameter in params.items():
            self.assertTrue(torch.isfinite(parameter.grad).all(), name)

    def test_variable_chain_backward_has_finite_gradients_for_all_parameters(self):
        chain = default_chain(["compressor", "distortion"])
        normalized = VariableChain.normalized_configuration(chain).requires_grad_()
        signal = _Signal(burst_signal())

        rendered = VariableChain(44_100, chain)(signal, normalized).audio_data
        rendered.square().mean().backward()

        self.assertEqual(normalized.shape, (1, 9))
        self.assertTrue(torch.isfinite(normalized.grad).all())


if __name__ == "__main__":
    unittest.main()
