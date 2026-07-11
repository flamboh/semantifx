from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from semantifx_inference.adapters.text2fx import _MODEL_CACHE, _install_model_cache, Text2FXAdapter
from semantifx_inference.errors import SemantiFXError
from semantifx_inference.cli import build_parser


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
    def setUp(self):
        _MODEL_CACHE.clear()

    def test_model_factory_is_cached_by_model_name(self):
        calls = []

        def load_model(name):
            calls.append(name)
            return object()

        module = SimpleNamespace(get_model=load_model)
        cached = _install_model_cache(module)
        first = module.get_model("ms_clap")
        second = module.get_model("ms_clap")
        other = module.get_model("laion_clap")
        self.assertIs(first, second)
        self.assertIsNot(first, other)
        self.assertEqual(calls, ["ms_clap", "laion_clap"])
        self.assertEqual(cached.cache_hits, 1)
        self.assertEqual(cached.cache_misses, 2)

    def test_installing_cache_twice_is_idempotent(self):
        module = SimpleNamespace(get_model=lambda name: object())
        first = _install_model_cache(module)
        second = _install_model_cache(module)
        self.assertIs(first, second)

    def test_cli_defaults_to_a_reproducible_seed(self):
        args = build_parser().parse_args(
            ["analyze", "--audio", "/tmp/input.wav", "--prompt", "bright"]
        )
        self.assertEqual(args.random_seed, 0)

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
