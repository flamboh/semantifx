import unittest
from pathlib import Path

from semantifx_inference.adapters.clap import load_optimization_audio


class _Signal:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.calls = []

    def to_mono(self):
        self.calls.append(("to_mono",))
        return self

    def resample(self, sample_rate: int):
        self.calls.append(("resample", sample_rate))
        return self

    def to(self, device: str):
        self.calls.append(("to", device))
        return self


class _Runtime:
    SAMPLE_RATE = 44100
    device = "test-device"

    def __init__(self) -> None:
        self.signal = None

    def AudioSignal(self, path: Path):
        self.signal = _Signal(path)
        return self.signal


class CLAPAdapterTests(unittest.TestCase):
    def test_capture_amplitude_is_not_peak_normalized(self) -> None:
        runtime = _Runtime()

        signal = load_optimization_audio(runtime, Path("capture.wav"))

        self.assertIs(signal, runtime.signal)
        self.assertEqual(
            [("to_mono",), ("resample", 44100), ("to", "test-device")],
            signal.calls,
        )


if __name__ == "__main__":
    unittest.main()
