import unittest

from semantifx_inference.catalogue import default_chain
from semantifx_inference.service import analyze


class ReverbMockTests(unittest.TestCase):
    def test_spatial_prompt_updates_every_reverb_instance(self):
        request = {
            "type": "analyze",
            "request_id": "mock-reverb-1",
            "prompt": "spacious and distant in a hall",
            "audio": {"path": "/tmp/input.wav"},
            "chain": default_chain(["reverb", "reverb"]),
            "options": {"backend": "mock"},
        }
        result, _ = analyze(request)
        reverb_entries = [item for item in result["parameters"] if item["effect"] == "reverb"]
        self.assertEqual(6, len(reverb_entries))
        for instance_id in ("reverb-1", "reverb-2"):
            values = {
                item["name"]: item["value"]
                for item in reverb_entries
                if item["effect_instance_id"] == instance_id
            }
            self.assertEqual(0.7, values["decay"])
            self.assertEqual(0.5, values["mix"])


if __name__ == "__main__":
    unittest.main()
