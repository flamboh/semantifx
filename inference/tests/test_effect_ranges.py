import unittest

import dasp_pytorch

from semantifx_inference._generated_effects import EFFECTS


class EffectRangeTests(unittest.TestCase):
    def test_generated_eq_ranges_match_dasp_parameter_order(self) -> None:
        generated = [
            (name, (minimum, maximum))
            for name, _unit, minimum, maximum, _default in EFFECTS[0][2]
        ]
        dasp = list(dasp_pytorch.ParametricEQ(sample_rate=44_100).param_ranges.items())

        self.assertEqual(18, len(generated))
        self.assertEqual(generated, dasp)

    def test_generated_reverb_ranges_are_macro_ranges(self) -> None:
        reverb = next(effect for effect in EFFECTS if effect[0] == "reverb")
        self.assertEqual(
            [
                ("decay", (0.0, 1.0)),
                ("damping", (0.0, 1.0)),
                ("mix", (0.0, 1.0)),
            ],
            [
                (name, (minimum, maximum))
                for name, _unit, minimum, maximum, _default in reverb[2]
            ],
        )



if __name__ == "__main__":
    unittest.main()
