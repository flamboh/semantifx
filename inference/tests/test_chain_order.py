import unittest

import torch

from semantifx_inference.chain_schema import (
    DISTORTION_THEN_EQ,
    default_chain,
)
from semantifx_inference.differentiable_chain import EqDistortionChain


class _Signal:
    def __init__(self, audio_data: torch.Tensor) -> None:
        self.audio_data = audio_data

    def clone(self):
        return _Signal(self.audio_data.clone())


def configured_chain(order=None):
    chain = default_chain() if order is None else default_chain(order)
    by_id = {item["instance_id"]: item for item in chain}
    by_id["eq-1"]["parameters"]["low_shelf_gain_db"] = 12.0
    by_id["distortion-1"]["parameters"].update(
        {"drive_db": 36.0, "character": 0.8, "mix": 1.0}
    )
    return chain


class ChainOrderTests(unittest.TestCase):
    def test_normalized_configuration_is_instance_addressed(self) -> None:
        eq_first = configured_chain()
        distortion_first = list(reversed(eq_first))
        torch.testing.assert_close(
            EqDistortionChain.normalized_configuration(eq_first),
            EqDistortionChain.normalized_configuration(distortion_first),
        )

    def test_reordering_changes_the_render(self) -> None:
        sample_rate = 48_000
        samples = torch.sin(torch.linspace(0.0, 80.0, 4096)).reshape(1, 1, -1) * 0.6
        signal = _Signal(samples)
        eq_first = configured_chain()
        distortion_first = configured_chain(DISTORTION_THEN_EQ)
        normalized = EqDistortionChain.normalized_configuration(eq_first)

        first = EqDistortionChain(sample_rate, eq_first)(signal, normalized).audio_data
        second = EqDistortionChain(sample_rate, distortion_first)(signal, normalized).audio_data

        self.assertGreater(float((first - second).abs().mean()), 1.0e-3)


if __name__ == "__main__":
    unittest.main()
