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
                {"effect": "eq", "name": "low_shelf_gain_db", "value": 2.5, "unit": "dB"}
            ]

            writer("optimizing", 17, 600, parameters)

            self.assertFalse(Path(str(destination) + ".tmp").exists())
            self.assertEqual(
                json.loads(destination.read_text()),
                {
                    "protocol_version": "1",
                    "type": "analysis_progress",
                    "request_id": "progress-test",
                    "phase": "optimizing",
                    "step": 17,
                    "total_steps": 600,
                    "parameters": parameters,
                },
            )


if __name__ == "__main__":
    unittest.main()
