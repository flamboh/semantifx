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


if __name__ == "__main__":
    unittest.main()
