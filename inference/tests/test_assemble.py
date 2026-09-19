import json
from pathlib import Path
import tempfile
import threading
import time
import unittest

from semantifx_inference.assembly import assemble
from semantifx_inference.errors import SemantiFXError
from semantifx_inference.planner import MockPlanner
from semantifx_inference.protocol import validate_assemble_request
from semantifx_inference.service import handle
from semantifx_inference.spool_worker import _write_json, run_spool
from semantifx_inference.progress import AnalysisCancelled


def assemble_request(request_id="assemble-1", prompt="warm"):
    return {
        "type": "assemble",
        "request_id": request_id,
        "prompt": prompt,
        "options": {"backend": "mock"},
    }


class AssembleRequestTests(unittest.TestCase):
    def test_validates_assemble_without_an_audio_or_chain(self):
        value = assemble_request()
        self.assertIs(validate_assemble_request(value), value)

    def test_rejects_analyze_fields_on_assemble(self):
        value = assemble_request()
        value["chain"] = []
        with self.assertRaises(SemantiFXError) as raised:
            validate_assemble_request(value)
        self.assertEqual(raised.exception.details["field"], "request")

    def test_mock_service_returns_a_default_chain_and_judgments(self):
        result, diagnostics = handle(assemble_request(prompt="warm and lightly saturated"))
        self.assertEqual(diagnostics, "")
        self.assertEqual(result["type"], "assembly_result")
        self.assertEqual(result["status"], "ok")
        self.assertEqual([item["effect_type"] for item in result["chain"]], ["eq", "distortion"])
        self.assertEqual(set(result["judgments"]), {"selection", "no_change", "order"})

    def test_no_change_returns_an_empty_chain(self):
        result, _ = handle(assemble_request(prompt="sounds good as it is"))
        self.assertEqual(result["chain"], [])
        self.assertGreaterEqual(result["judgments"]["no_change"], 0.5)

    def test_unknown_request_type_is_an_invalid_request(self):
        result, _ = handle({"type": "something_else", "request_id": "bad", "prompt": "x"})
        self.assertEqual(result["error"]["code"], "invalid_request")
        self.assertEqual(result["error"]["details"]["field"], "type")

    def test_mock_planner_warns_when_argmax_fallback_is_used(self):
        chain, judgments, warnings = assemble(
            "unmatched words only",
            MockPlanner(),
        )
        self.assertEqual(chain[0]["effect_type"], "eq")
        self.assertEqual(judgments.selection["eq"], 0.1)
        self.assertEqual(
            warnings,
            ["No effect cleared the selection threshold; picked eq by argmax."],
        )


class LatestRequestTests(unittest.TestCase):
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
        self.fail("worker did not publish the requested result")

    def test_new_request_cancels_and_then_replaces_running_request(self):
        calls = []
        cancellations = []

        def analyzer(value, backend, progress):
            request_id = value["request_id"]
            calls.append(request_id)
            if request_id == "first":
                progress("optimizing", 1, 2, [])
                _write_json(directory / "request.json", assemble_request("second", "bright"))
                progress("optimizing", 2, 2, [])
                self.fail("the first request should have been cancelled")
            return ({"type": "assembly_result", "request_id": request_id, "status": "ok"}, "")

        def recording_analyzer(value, backend, progress):
            try:
                return analyzer(value, backend, progress)
            except AnalysisCancelled:
                cancellations.append(value["request_id"])
                raise

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            worker = threading.Thread(
                target=run_spool,
                args=(directory,),
                kwargs={
                    "default_backend": "mock",
                    "poll_seconds": 0.005,
                    "analyzer": recording_analyzer,
                },
            )
            worker.start()
            try:
                _write_json(directory / "request.json", assemble_request("first"))
                second = self.wait_for_result(directory, "second")
            finally:
                (directory / "shutdown").touch()
                worker.join(timeout=2.0)

        self.assertFalse(worker.is_alive())
        self.assertEqual(calls, ["first", "second"])
        self.assertEqual(cancellations, ["first"])
        self.assertEqual(second["request_id"], "second")


if __name__ == "__main__":
    unittest.main()
