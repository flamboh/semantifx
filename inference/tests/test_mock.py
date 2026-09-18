import unittest

from semantifx_inference.catalogue import default_chain
from semantifx_inference.service import analyze


def request(prompt="warm", path="/tmp/input.wav"):
    return {
        "type": "analyze",
        "request_id": "mock-1",
        "prompt": prompt,
        "audio": {"path": path},
        "chain": default_chain(),
        "options": {"backend": "mock"},
    }


class MockAdapterTests(unittest.TestCase):
    def test_produces_complete_eq_distortion_chain(self):
        result, _ = analyze(request("warm and gritty"))
        self.assertEqual(len(result["parameters"]), 22)
        distortion = {
            item["name"]: item["value"]
            for item in result["parameters"]
            if item["effect_instance_id"] == "distortion-1"
        }
        self.assertGreater(distortion["drive_db"], 0.0)
        self.assertGreater(distortion["mix"], 0.25)

    def test_echoes_reversed_chain_order(self):
        value = request("warm")
        value["chain"].reverse()
        result, _ = analyze(value)
        self.assertEqual(result["chain_order"], ["distortion-1", "eq-1"])

    def test_produces_full_named_eq_shape(self):
        result, diagnostics = analyze(request("warm and clear"))
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["backend"], "mock")
        self.assertEqual(len(result["parameters"]), 22)
        names = {item["name"] for item in result["parameters"]}
        self.assertIn("low_shelf_gain_db", names)
        self.assertIn("high_shelf_q_factor", names)
        self.assertEqual(result["metrics"]["matched_profile_words"], ["warm", "clear"])
        self.assertIn("warm", diagnostics)

    def test_is_deterministic_except_for_timing(self):
        first, _ = analyze(request("shape it like violet fog", "/same.wav"))
        second, _ = analyze(request("shape it like violet fog", "/same.wav"))
        self.assertEqual(first["parameters"], second["parameters"])
        self.assertEqual(first["metrics"], second["metrics"])

    def test_semantic_profile_changes_curve(self):
        warm, _ = analyze(request("warm"))
        bright, _ = analyze(request("bright"))
        warm_values = {item["name"]: item["value"] for item in warm["parameters"]}
        bright_values = {item["name"]: item["value"] for item in bright["parameters"]}
        self.assertGreater(warm_values["low_shelf_gain_db"], bright_values["low_shelf_gain_db"])
        self.assertGreater(bright_values["high_shelf_gain_db"], warm_values["high_shelf_gain_db"])


if __name__ == "__main__":
    unittest.main()

    def test_empty_chain_is_a_no_op(self):
        value = request("warm")
        value["chain"] = []
        result, _ = analyze(value)
        self.assertEqual(result["parameters"], [])
        self.assertEqual(result["metrics"]["iterations"], 0)
        self.assertEqual(result["warnings"], ["The chain is empty; nothing to optimize."])

    def test_applies_profiles_to_repeated_instances_in_chain_order(self):
        value = request("warm")
        value["chain"] = default_chain(["eq", "distortion", "eq"])
        result, _ = analyze(value)
        self.assertEqual(result["chain_order"], ["eq-1", "distortion-1", "eq-2"])
        eq_entries = [
            item for item in result["parameters"] if item["effect"] == "eq"
        ]
        self.assertEqual(
            [item["effect_instance_id"] for item in eq_entries[::18]],
            ["eq-1", "eq-2"],
        )
