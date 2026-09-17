import subprocess
import sys
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unittest
import wave


FIXTURE = Path(__file__).parent / "fixtures" / "golden_analysis.json"


def write_test_audio(path: Path, specification: dict) -> None:
    sample_rate = specification["sample_rate"]
    frequencies = specification["frequencies_hz"]
    amplitude = specification["amplitude"]
    frames = bytearray()
    for index in range(round(sample_rate * specification["duration_seconds"])):
        sample = amplitude * sum(
            math.sin(2 * math.pi * frequency * index / sample_rate)
            for frequency in frequencies
        ) / len(frequencies)
        frames.extend(struct.pack("<h", round(32767 * sample)))

    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(sample_rate)
        output.writeframes(frames)


def assert_json_matches(test_case: unittest.TestCase, expected, actual, path="") -> None:
    if isinstance(expected, dict):
        if not isinstance(actual, dict):
            test_case.fail(f"{path}: actual={actual!r}, expected={expected!r}")
        if set(expected) != set(actual):
            test_case.fail(f"{path}: actual keys={set(actual)!r}, expected keys={set(expected)!r}")
        for key in expected:
            child_path = f"{path}.{key}" if path else key
            assert_json_matches(test_case, expected[key], actual[key], child_path)
    elif isinstance(expected, list):
        if not isinstance(actual, list):
            test_case.fail(f"{path}: actual={actual!r}, expected={expected!r}")
        if len(expected) != len(actual):
            test_case.fail(f"{path}: actual length={len(actual)}, expected length={len(expected)}")
        for index, (expected_item, actual_item) in enumerate(zip(expected, actual)):
            assert_json_matches(test_case, expected_item, actual_item, f"{path}[{index}]")
    elif isinstance(expected, float) and isinstance(actual, float):
        if not math.isclose(actual, expected, rel_tol=1e-6, abs_tol=1e-9):
            test_case.fail(f"{path}: actual={actual!r}, expected={expected!r}")
    elif type(expected) is not type(actual) or actual != expected:
        test_case.fail(f"{path}: actual={actual!r}, expected={expected!r}")

@unittest.skipUnless(
    os.environ.get("SEMANTIFX_RUN_MODEL_TESTS") == "1",
    "set SEMANTIFX_RUN_MODEL_TESTS=1 to run the MS-CLAP golden test",
)
class GoldenReproducibilityTests(unittest.TestCase):
    """The golden run stays on CPU because CUDA's bicubic backward is nondeterministic.

    The optimizer amplifies that noise. CPU is bit-exact and no slower for this fixture.
    The small tolerance absorbs same-machine float slack, not device changes.
    """
    def test_seeded_analysis_matches_golden_result(self) -> None:
        fixture = json.loads(FIXTURE.read_text())
        with tempfile.TemporaryDirectory() as directory:
            audio_path = Path(directory) / "golden-input.wav"
            write_test_audio(audio_path, fixture["inputs"]["audio_synthesis"])
            arguments = [
                str(audio_path) if argument == "{audio}" else argument
                for argument in fixture["inputs"]["arguments"]
            ]

            completed = subprocess.run(
                [sys.executable, "-m", "semantifx_inference.cli", *arguments],
                cwd=Path(__file__).parents[1],
                env={**os.environ, "CUDA_VISIBLE_DEVICES": ""},
                capture_output=True,
                text=True,
            )

        self.assertEqual(0, completed.returncode, completed.stderr)
        actual = json.loads(completed.stdout)
        assert_json_matches(self, fixture["result"], actual)


if __name__ == "__main__":
    unittest.main()
