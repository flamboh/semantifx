import unittest

import dasp_pytorch
import torch

from semantifx_inference.catalogue import CATALOGUE, default_chain
from semantifx_inference.differentiable_chain import VariableChain
from semantifx_inference.distortion import process as process_distortion


class _Signal:
    def __init__(self, audio_data: torch.Tensor) -> None:
        self.audio_data = audio_data

    def clone(self):
        return _Signal(self.audio_data.clone())


def configured_chain(types):
    chain = default_chain(types)
    for item in chain:
        if item["effect_type"] == "eq":
            item["parameters"]["low_shelf_gain_db"] = 12.0
        else:
            item["parameters"].update(
                {"drive_db": 36.0, "character": 0.8, "mix": 1.0}
            )
    return chain


def reference_render(sample_rate, chain, normalized, audio):
    rendered = audio
    offset = 0
    for item in chain:
        definition = CATALOGUE[item["effect_type"]]
        physical = {}
        for spec in definition.specs:
            physical[spec.name] = spec.minimum + normalized[:, offset] * (
                spec.maximum - spec.minimum
            )
            offset += 1
        if item["effect_type"] == "eq":
            rendered = dasp_pytorch.functional.parametric_eq(rendered, sample_rate, **physical)
        else:
            rendered = process_distortion(rendered, sample_rate, **physical)
    return rendered


class VariableChainTests(unittest.TestCase):
    def test_render_matches_independent_reference_in_both_orders(self):
        sample_rate = 48_000
        audio = torch.sin(torch.linspace(0.0, 80.0, 4096)).reshape(1, 1, -1) * 0.6
        for types in (["eq", "distortion"], ["distortion", "eq"]):
            chain = configured_chain(types)
            normalized = VariableChain.normalized_configuration(chain)
            signal = _Signal(audio)
            actual = VariableChain(sample_rate, chain)(signal, normalized).audio_data
            expected = reference_render(sample_rate, chain, normalized, audio)
            self.assertTrue(torch.equal(actual, expected))

    def test_empty_chain_is_a_no_op(self):
        chain = VariableChain(44_100, [])
        audio = torch.randn(1, 1, 32)
        signal = _Signal(audio)
        normalized = chain.normalized_configuration([])
        self.assertEqual(tuple(normalized.shape), (1, 0))
        self.assertTrue(torch.equal(chain(signal, normalized).audio_data, audio))
        self.assertEqual(chain.snapshot(normalized), [])

    def test_repeated_type_instances_have_distinct_snapshots(self):
        chain = default_chain(["eq", "eq"])
        chain[0]["parameters"]["low_shelf_gain_db"] = 5.0
        chain[1]["parameters"]["low_shelf_gain_db"] = -5.0
        module = VariableChain(44_100, chain)
        snapshot = module.snapshot(module.normalized_configuration(chain))
        values = [
            item["value"]
            for item in snapshot
            if item["name"] == "low_shelf_gain_db"
        ]
        self.assertEqual(values, [5.0, -5.0])


if __name__ == "__main__":
    unittest.main()
