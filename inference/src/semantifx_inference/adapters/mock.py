import hashlib
import random
from typing import Dict, List, Tuple


_DEFAULTS = {
    "low_shelf_gain_db": 0.0,
    "low_shelf_cutoff_freq": 180.0,
    "low_shelf_q_factor": 0.71,
    "band0_gain_db": 0.0,
    "band0_cutoff_freq": 600.0,
    "band0_q_factor": 1.0,
    "band1_gain_db": 0.0,
    "band1_cutoff_freq": 3000.0,
    "band1_q_factor": 1.0,
    "band2_gain_db": 0.0,
    "band2_cutoff_freq": 9000.0,
    "band2_q_factor": 1.0,
    "band3_gain_db": 0.0,
    "band3_cutoff_freq": 15000.0,
    "band3_q_factor": 1.0,
    "high_shelf_gain_db": 0.0,
    "high_shelf_cutoff_freq": 8000.0,
    "high_shelf_q_factor": 0.71,
}

_PROFILES = {
    "warm": {"low_shelf_gain_db": 4.0, "band0_gain_db": 1.5, "high_shelf_gain_db": -1.5},
    "bright": {"band2_gain_db": 2.0, "high_shelf_gain_db": 4.5},
    "airy": {"band3_gain_db": 2.0, "high_shelf_gain_db": 5.0},
    "dark": {"band2_gain_db": -2.0, "high_shelf_gain_db": -5.0},
    "muffled": {"band1_gain_db": -1.5, "band2_gain_db": -4.0, "high_shelf_gain_db": -7.0},
    "thin": {"low_shelf_gain_db": -5.0, "band0_gain_db": -2.0},
    "full": {"low_shelf_gain_db": 3.5, "band0_gain_db": 2.0},
    "clear": {"band0_gain_db": -1.0, "band1_gain_db": 2.5, "high_shelf_gain_db": 1.0},
    "present": {"band1_gain_db": 3.5, "band1_cutoff_freq": 4000.0},
    "punchy": {"low_shelf_gain_db": 2.0, "band0_gain_db": 3.0, "band0_q_factor": 1.4},
}


def _unit(name: str) -> str:
    if name.endswith("_gain_db"):
        return "dB"
    if name.endswith("_freq"):
        return "Hz"
    if name.endswith("_q_factor"):
        return "ratio"
    return "unitless"


class MockAdapter:
    name = "mock"

    def analyze(self, request: dict) -> Tuple[List[dict], Dict[str, object], List[str], str]:
        prompt = request["prompt"].lower()
        values = dict(_DEFAULTS)
        matched = []
        for word, changes in _PROFILES.items():
            if word in prompt:
                matched.append(word)
                for name, value in changes.items():
                    if name.endswith("_gain_db"):
                        values[name] += value
                    else:
                        values[name] = value

        seed_material = "%s\0%s" % (request["prompt"], request["audio"]["path"])
        seed = int.from_bytes(hashlib.sha256(seed_material.encode("utf-8")).digest()[:8], "big")
        rng = random.Random(seed)
        if not matched:
            # Make arbitrary prompts visibly deterministic without pretending to
            # understand them. Keep the fallback curve intentionally gentle.
            for name in ("low_shelf_gain_db", "band0_gain_db", "band1_gain_db", "high_shelf_gain_db"):
                values[name] = round(rng.uniform(-1.5, 1.5), 2)

        parameters = [
            {"effect": "eq", "name": name, "value": float(value), "unit": _unit(name)}
            for name, value in values.items()
        ]
        diagnostics = "mock profile words: %s" % (", ".join(matched) if matched else "none")
        return (
            parameters,
            {"deterministic": True, "matched_profile_words": matched},
            ["Mock parameters are for integration testing and are not ML inference."],
            diagnostics,
        )

