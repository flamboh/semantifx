import unittest

from semantifx_inference.service import analyze


def request(prompt="warm", path="/tmp/input.wav"):
    return {
        "protocol_version": "1",
        "type": "analyze",
        "request_id": "mock-1",
        "prompt": prompt,
        "audio": {"path": path},
        "effects": ["eq"],
        "options": {"backend": "mock"},
    }


class MockAdapterTests(unittest.TestCase):
    def test_produces_full_named_eq_shape(self):
        result, diagnostics = analyze(request("warm and clear"))
        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["backend"], "mock")
        self.assertEqual(len(result["parameters"]), 18)
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

