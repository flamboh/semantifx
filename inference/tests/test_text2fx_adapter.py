from pathlib import Path
import tempfile
import unittest

from semantifx_inference.adapters.text2fx import Text2FXAdapter
from semantifx_inference.errors import SemantiFXError


def request(path):
    return {
        "protocol_version": "1",
        "type": "analyze",
        "request_id": "text2fx-test",
        "prompt": "warm",
        "audio": {"path": path},
        "effects": ["eq"],
    }


class Text2FXAdapterErrorTests(unittest.TestCase):
    def test_missing_audio_is_actionable(self):
        adapter = Text2FXAdapter(repo=Path("/checkout/does/not/matter"))
        with self.assertRaises(SemantiFXError) as raised:
            adapter.analyze(request("/audio/does/not/exist.wav"))
        self.assertEqual(raised.exception.code, "audio_not_found")
        self.assertTrue(raised.exception.remediation)

    def test_missing_checkout_is_actionable(self):
        with tempfile.NamedTemporaryFile(suffix=".wav") as audio:
            adapter = Text2FXAdapter(repo=Path(audio.name + "-missing-checkout"))
            with self.assertRaises(SemantiFXError) as raised:
                adapter.analyze(request(audio.name))
        self.assertEqual(raised.exception.code, "text2fx_checkout_not_found")
        self.assertIn("SEMANTIFX_TEXT2FX_REPO", raised.exception.remediation[0])


if __name__ == "__main__":
    unittest.main()

