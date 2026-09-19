import hashlib
import json
from pathlib import Path
import unittest

import torch
import torch.nn.functional as F

from semantifx_inference.catalogue import default_chain
from semantifx_inference.differentiable_chain import VariableChain
from semantifx_inference.effects import reverb


REPO = Path(__file__).resolve().parents[2]
NOISE_PATH = REPO / "protocol" / "reverb" / "noise_44100.f32"
NOISE_METADATA_PATH = REPO / "protocol" / "reverb" / "noise_44100.json"


class _Signal:
    def __init__(self, audio_data: torch.Tensor) -> None:
        self.audio_data = audio_data

    def clone(self):
        return _Signal(self.audio_data.clone())


class ReverbTests(unittest.TestCase):
    def test_frozen_noise_table_shape_hash_and_statistics(self):
        metadata = json.loads(NOISE_METADATA_PATH.read_text(encoding="utf-8"))
        raw = NOISE_PATH.read_bytes()
        self.assertEqual(metadata["sha256"], hashlib.sha256(raw).hexdigest())
        self.assertEqual(metadata["sample_rate"], 44_100)
        self.assertEqual(metadata["channels"], 2)
        self.assertEqual(metadata["bands"], 12)
        self.assertEqual(metadata["samples"], 65_536)
        self.assertEqual(metadata["seed"], 20_260_918)
        self.assertEqual(metadata["taps"], 1023)

        table = torch.frombuffer(bytearray(raw), dtype=torch.float32).reshape(2, 12, 65_536)
        self.assertTrue(torch.isfinite(table).all())
        rms = table.square().mean(dim=-1).sqrt()
        self.assertTrue(torch.all((rms > 1e-4) & (rms < 1.0)))

    def test_macro_expansion_matches_the_spec(self):
        decay = torch.tensor([0.5])
        damping = torch.tensor([0.3])
        gains, decays = reverb.expand_macros(decay, damping)
        positions = torch.arange(12, dtype=torch.float32) / 11.0
        self.assertTrue(torch.allclose(gains[0], 1.0 - 0.3 * positions))
        self.assertTrue(torch.allclose(decays[0], 0.5 + 0.3 * positions))

    def test_render_has_finite_nonzero_macro_gradients_at_defaults(self):
        signal = torch.sin(torch.linspace(0.0, 19.0, 256)).reshape(1, 1, -1)
        decay = torch.tensor([0.5], requires_grad=True)
        damping = torch.tensor([0.3], requires_grad=True)
        mix = torch.tensor([0.3], requires_grad=True)
        output = reverb.render(
            signal,
            44_100,
            {"decay": decay, "damping": damping, "mix": mix},
        )
        output.square().mean().backward()
        for parameter in (decay, damping, mix):
            self.assertIsNotNone(parameter.grad)
            self.assertTrue(torch.isfinite(parameter.grad).all())
            self.assertNotEqual(parameter.grad.abs().item(), 0.0)

    def test_render_rejects_unsupported_sample_rate(self):
        with self.assertRaisesRegex(ValueError, "44,?100"):
            reverb.render(
                torch.zeros(1, 1, 8),
                48_000,
                {
                    "decay": torch.tensor([0.5]),
                    "damping": torch.tensor([0.3]),
                    "mix": torch.tensor([0.3]),
                },
            )

    def test_fft_convolution_matches_direct_convolution(self):
        noise = reverb.load_noise_table()
        gains, decays = reverb.expand_macros(torch.tensor([0.5]), torch.tensor([0.3]))
        impulse = reverb.build_impulse_response(gains[0], decays[0], noise)
        audio = torch.randn(1, 2, 32)
        actual = reverb.fft_convolve(audio, impulse.unsqueeze(0))
        padded = F.pad(audio, (impulse.shape[-1] - 1, 0))
        expected = F.conv1d(
            padded,
            torch.flip(impulse, dims=[-1]).unsqueeze(1),
            groups=2,
        )
        self.assertTrue(torch.allclose(actual, expected, atol=1e-5, rtol=1e-5))

    def test_eq_and_reverb_chain_has_finite_gradients_for_all_parameters(self):
        chain = default_chain(["eq", "reverb"])
        module = VariableChain(44_100, chain)
        self.assertEqual(module.num_params, 21)
        time = torch.linspace(0.0, 40.0, 512)
        audio = (0.4 * torch.sin(time) + 0.2 * torch.cos(3.0 * time)).reshape(1, 1, -1)
        normalized = torch.full((1, module.num_params), 0.5, requires_grad=True)
        output = module(_Signal(audio), normalized).audio_data
        output.square().mean().backward()
        self.assertIsNotNone(normalized.grad)
        self.assertTrue(torch.isfinite(normalized.grad).all())


if __name__ == "__main__":
    unittest.main()
