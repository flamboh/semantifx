import io
import json
import unittest

from semantifx_inference.chain_schema import default_chain
from semantifx_inference.worker import run_stream


class WorkerTests(unittest.TestCase):
    def test_one_response_per_nonempty_input_line(self):
        valid = {
                "type": "analyze",
            "request_id": "worker-1",
            "prompt": "dark",
            "audio": {"path": "/tmp/input.wav"},
            "chain": default_chain(),
        }
        source = [json.dumps(valid) + "\n", "\n", "not-json\n"]
        destination = io.StringIO()
        count = run_stream(source, destination)
        responses = [json.loads(line) for line in destination.getvalue().splitlines()]
        self.assertEqual(count, 2)
        self.assertEqual(len(responses), 2)
        self.assertEqual(responses[0]["request_id"], "worker-1")
        self.assertEqual(responses[0]["status"], "ok")
        self.assertEqual(responses[1]["error"]["code"], "invalid_json")
        self.assertIsNone(responses[1]["request_id"])

    def test_diagnostics_are_separate_from_json_stream(self):
        valid = {
                "type": "analyze",
            "request_id": "worker-2",
            "prompt": "airy",
            "audio": {"path": "/tmp/input.wav"},
            "chain": default_chain(),
        }
        destination = io.StringIO()
        diagnostics = io.StringIO()
        run_stream([json.dumps(valid)], destination, diagnostics_stream=diagnostics)
        json.loads(destination.getvalue())
        self.assertIn("request worker-2", diagnostics.getvalue())
        self.assertNotIn("mock profile", destination.getvalue())


if __name__ == "__main__":
    unittest.main()
