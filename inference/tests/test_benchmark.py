import csv
import json
from pathlib import Path
import tempfile
import unittest

from semantifx_inference.benchmark import BenchmarkError, expand_matrix, load_manifest, run_benchmark


def manifest():
    return {
        "manifest_version": "1",
        "name": "unit-test",
        "repetitions": 1,
        "seed": 42,
        "audio": [{"id": "clip", "path": "fixtures/clip.wav"}],
        "prompts": [
            {"id": "warm", "text": "warm"},
            {"id": "bright", "text": "bright"},
        ],
        "matrix": {
            "backend": ["mock"],
            "iterations": [1, 2],
            "parameter_initialization": ["zeros"],
        },
    }


class BenchmarkTests(unittest.TestCase):
    def test_matrix_expansion_has_stable_key_order(self):
        expanded = expand_matrix({"iterations": [1, 2], "backend": ["mock", "text2fx"]})
        self.assertEqual(
            expanded,
            [
                {"backend": "mock", "iterations": 1},
                {"backend": "mock", "iterations": 2},
                {"backend": "text2fx", "iterations": 1},
                {"backend": "text2fx", "iterations": 2},
            ],
        )

    def test_mock_benchmark_writes_jsonl_and_csv(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest_path = root / "manifest.json"
            jsonl_path = root / "results" / "benchmark.jsonl"
            csv_path = root / "results" / "benchmark.csv"
            manifest_path.write_text(json.dumps(manifest()), encoding="utf-8")
            count = run_benchmark(manifest_path, jsonl_path, csv_path)

            records = [json.loads(line) for line in jsonl_path.read_text(encoding="utf-8").splitlines()]
            with csv_path.open(encoding="utf-8", newline="") as source:
                rows = list(csv.DictReader(source))
            self.assertEqual(count, 4)
            self.assertEqual(len(records), 4)
            self.assertEqual(len(rows), 4)
            self.assertTrue(all(record["status"] == "ok" for record in records))
            self.assertTrue(all(len(record["parameters"]) == 18 for record in records))
            expected_audio = str((root / "fixtures" / "clip.wav").resolve())
            self.assertTrue(all(record["audio"]["path"] == expected_audio for record in records))
            self.assertNotIn("diagnostics", records[0])
            self.assertEqual(rows[0]["parameter_count"], "18")
            self.assertTrue(all(isinstance(record["random_seed"], int) for record in records))

    def test_manifest_rejects_unknown_matrix_option(self):
        value = manifest()
        value["matrix"]["not_an_option"] = [True]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(value), encoding="utf-8")
            with self.assertRaises(BenchmarkError) as raised:
                load_manifest(path)
        self.assertIn("not_an_option", str(raised.exception))


if __name__ == "__main__":
    unittest.main()
