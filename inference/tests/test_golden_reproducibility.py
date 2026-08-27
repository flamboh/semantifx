import contextlib
import io
import json
import math
import os
from pathlib import Path
import struct
import tempfile
import unittest
import wave

from semantifx_inference.cli import main


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


@unittest.skipUnless(
    os.environ.get("SEMANTIFX_RUN_MODEL_TESTS") == "1",
    "set SEMANTIFX_RUN_MODEL_TESTS=1 to run the MS-CLAP golden test",
)
class GoldenReproducibilityTests(unittest.TestCase):
    def test_seeded_analysis_matches_golden_result(self) -> None:
        fixture = json.loads(FIXTURE.read_text())
        with tempfile.TemporaryDirectory() as directory:
            audio_path = Path(directory) / "golden-input.wav"
            write_test_audio(audio_path, fixture["inputs"]["audio_synthesis"])
            arguments = [
                str(audio_path) if argument == "{audio}" else argument
                for argument in fixture["inputs"]["arguments"]
            ]

            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                exit_code = main(arguments)

        self.assertEqual(0, exit_code)
        self.assertEqual(fixture["result"], json.loads(stdout.getvalue()))


if __name__ == "__main__":
    unittest.main()
