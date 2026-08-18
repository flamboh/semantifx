import json
from pathlib import Path
import tempfile
import threading
import time
import unittest

from semantifx_inference.chain_schema import default_chain
from semantifx_inference.spool_worker import _write_json, process_request, run_spool


def request(request_id):
    return {
        "protocol_version": "2",
        "type": "analyze",
        "request_id": request_id,
        "prompt": "warm",
        "audio": {"path": "/tmp/input.wav"},
        "chain": default_chain(),
        "options": {"iterations": 600},
    }


class SpoolWorkerTests(unittest.TestCase):
    def wait_for_result(self, directory, request_id):
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            try:
                value = json.loads((directory / "result.json").read_text())
            except (FileNotFoundError, ValueError):
                time.sleep(0.01)
                continue
            if value.get("request_id") == request_id:
                return value
        self.fail("persistent worker did not publish a result")

    def test_processes_sequential_requests_without_replacing_the_analyzer(self):
        calls = []

        def analyzer(value, backend, progress):
            calls.append(value["request_id"])
            return ({"type": "analysis_result", "request_id": value["request_id"]}, "")

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            process_request(request("one"), directory, analyzer=analyzer)
            first = json.loads((directory / "result.json").read_text())
            process_request(request("two"), directory, analyzer=analyzer)
            second = json.loads((directory / "result.json").read_text())

        self.assertEqual(calls, ["one", "two"])
        self.assertEqual(first["request_id"], "one")
        self.assertEqual(second["request_id"], "two")

    def test_matching_stop_marker_cancels_after_publishing_latest_progress(self):
        def analyzer(value, backend, progress):
            progress("optimizing", 1, 600, [{"name": "first"}])
            _write_json(directory / "stop.json", {"request_id": value["request_id"]})
            progress("optimizing", 2, 600, [{"name": "second"}])
            self.fail("cancellation should stop the analyzer")

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            process_request(request("cancel-me"), directory, analyzer=analyzer)
            result = json.loads((directory / "result.json").read_text())
            progress = json.loads((directory / "progress.json").read_text())

        self.assertEqual(result["type"], "analysis_cancelled")
        self.assertEqual(result["request_id"], "cancel-me")
        self.assertEqual(progress["request_id"], "cancel-me")
        self.assertEqual(progress["step"], 2)

    def test_stop_after_analyzer_returns_wins_over_final_result(self):
        def analyzer(value, backend, progress):
            progress("optimizing", 600, 600, [{"name": "final"}])
            _write_json(directory / "stop.json", {"request_id": value["request_id"]})
            return ({"type": "analysis_result", "request_id": value["request_id"]}, "")

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            process_request(request("late-stop"), directory, analyzer=analyzer)
            result = json.loads((directory / "result.json").read_text())

        self.assertEqual(result["type"], "analysis_cancelled")

    def test_malformed_options_do_not_crash_before_protocol_validation(self):
        malformed = request("malformed")
        malformed["options"] = "not-an-object"

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            process_request(malformed, directory, default_backend="mock")
            result = json.loads((directory / "result.json").read_text())

        self.assertEqual(result["status"], "error")
        self.assertEqual(result["error"]["code"], "invalid_request")

    def test_stop_for_another_request_is_ignored(self):
        def analyzer(value, backend, progress):
            _write_json(directory / "stop.json", {"request_id": "stale"})
            progress("optimizing", 1, 1)
            return ({"type": "analysis_result", "request_id": value["request_id"]}, "")

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            process_request(request("current"), directory, analyzer=analyzer)
            result = json.loads((directory / "result.json").read_text())

        self.assertEqual(result["type"], "analysis_result")

    def test_spool_loop_accepts_two_requests_in_one_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            worker = threading.Thread(
                target=run_spool,
                args=(directory,),
                kwargs={"default_backend": "mock", "poll_seconds": 0.005},
            )
            worker.start()
            try:
                _write_json(directory / "request.json", request("first"))
                first = self.wait_for_result(directory, "first")
                (directory / "result.json").unlink()
                _write_json(directory / "request.json", request("second"))
                second = self.wait_for_result(directory, "second")
            finally:
                (directory / "shutdown").touch()
                worker.join(timeout=2.0)

        self.assertFalse(worker.is_alive())
        self.assertEqual(first["status"], "ok")
        self.assertEqual(second["status"], "ok")

    def test_malformed_request_does_not_prevent_the_next_request(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            worker = threading.Thread(
                target=run_spool,
                args=(directory,),
                kwargs={"default_backend": "mock", "poll_seconds": 0.005},
            )
            worker.start()
            try:
                malformed = request("bad")
                malformed["options"] = "not-an-object"
                _write_json(directory / "request.json", malformed)
                first = self.wait_for_result(directory, "bad")
                (directory / "result.json").unlink()
                _write_json(directory / "request.json", request("good"))
                second = self.wait_for_result(directory, "good")
            finally:
                (directory / "shutdown").touch()
                worker.join(timeout=2.0)

        self.assertEqual(first["status"], "error")
        self.assertEqual(second["status"], "ok")


if __name__ == "__main__":
    unittest.main()
