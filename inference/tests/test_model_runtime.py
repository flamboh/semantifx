from types import SimpleNamespace
import unittest

from semantifx_inference.model_runtime import _MODEL_CACHE, install_model_cache


class ModelRuntimeTests(unittest.TestCase):
    def setUp(self) -> None:
        _MODEL_CACHE.clear()

    def test_model_factory_is_cached_by_model_name(self) -> None:
        calls = []

        def get_model(name):
            calls.append(name)
            return object()

        runtime = SimpleNamespace(get_model=get_model)
        cached = install_model_cache(runtime)

        first = cached("ms_clap")
        second = cached("ms_clap")
        other = cached("laion_clap")

        self.assertIs(first, second)
        self.assertIsNot(first, other)
        self.assertEqual(calls, ["ms_clap", "laion_clap"])
        self.assertEqual(cached.cache_hits, 1)
        self.assertEqual(cached.cache_misses, 2)

    def test_installation_is_idempotent(self) -> None:
        runtime = SimpleNamespace(get_model=lambda _name: object())
        first = install_model_cache(runtime)
        second = install_model_cache(runtime)
        self.assertIs(first, second)


if __name__ == "__main__":
    unittest.main()
