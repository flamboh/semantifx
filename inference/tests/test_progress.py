import json
from pathlib import Path
import tempfile
import unittest

from semantifx_inference.progress import ProgressFileWriter


class ProgressFileWriterTests(unittest.TestCase):
    def test_publishes_latest_snapshot_atomically(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "progress.json"
            writer = ProgressFileWriter(destination, "progress-test")
            parameters = [
                {
                    "effect_instance_id": "eq-1",
                    "effect": "eq",
                    "name": "low_shelf_gain_db",
                    "value": 2.5,
                    "unit": "dB",
                }
            ]

            writer("optimizing", 17, 600, parameters)

            self.assertFalse(Path(str(destination) + ".tmp").exists())
            self.assertEqual(
                json.loads(destination.read_text()),
                {
                    "protocol_version": "2",
                    "type": "analysis_progress",
                    "request_id": "progress-test",
                    "phase": "optimizing",
                    "step": 17,
                    "total_steps": 600,
                    "chain_order": ["eq-1", "distortion-1"],
                    "parameters": parameters,
                },
            )

    def test_uses_the_current_protocol_version(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "progress.json"
            ProgressFileWriter(destination, "chain-progress")("loading_model", 0, 10)
            self.assertEqual(json.loads(destination.read_text())["protocol_version"], "2")

    def test_publishes_reversed_effect_order(self):
        with tempfile.TemporaryDirectory() as directory:
            destination = Path(directory) / "progress.json"
            ProgressFileWriter(
                destination, "chain-progress", ["distortion-1", "eq-1"]
            )("loading_model", 0, 10)
            self.assertEqual(
                json.loads(destination.read_text())["chain_order"],
                ["distortion-1", "eq-1"],
            )


if __name__ == "__main__":
    unittest.main()
