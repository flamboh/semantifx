import hashlib
import random
from typing import Dict, List, Tuple

from ..catalogue import parameter_entries

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


class MockAdapter:
    name = "mock"

    def __init__(self, progress=None) -> None:
        self.progress = progress

    def analyze(self, request: dict) -> Tuple[List[dict], Dict[str, object], List[str], str]:
        prompt = request["prompt"].lower()
        chain = request["chain"]
        if not chain:
            if self.progress is not None:
                self.progress("finalizing", 0, 0, [])
            return [], {"iterations": 0}, ["The chain is empty; nothing to optimize."], ""
        chain_values = {
            item["instance_id"]: dict(item["parameters"])
            for item in chain
        }
        matched = []
        for word, changes in _PROFILES.items():
            if word in prompt:
                matched.append(word)
                for item in chain:
                    if item["effect_type"] != "eq":
                        continue
                    values = chain_values[item["instance_id"]]
                    for name, value in changes.items():
                        if name.endswith("_gain_db"):
                            values[name] += value
                        else:
                            values[name] = value

        seed_material = "%s\0%s" % (request["prompt"], request["audio"]["path"])
        seed = int.from_bytes(hashlib.sha256(seed_material.encode("utf-8")).digest()[:8], "big")
        rng = random.Random(seed)
        if not matched:
            for item in chain:
                if item["effect_type"] != "eq":
                    continue
                values = chain_values[item["instance_id"]]
                for name in ("low_shelf_gain_db", "band0_gain_db", "band1_gain_db", "high_shelf_gain_db"):
                    values[name] = round(rng.uniform(-1.5, 1.5), 2)

        for item in chain:
            if item["effect_type"] != "distortion":
                continue
            distortion = chain_values[item["instance_id"]]
            if "warm" in prompt:
                distortion.update({"drive_db": 8.0, "character": 0.35, "tone_db": -2.0, "mix": 0.35})
            if any(word in prompt for word in ("gritty", "aggressive", "distorted", "crunchy")):
                distortion.update({"drive_db": 28.0, "character": 0.8, "tone_db": 2.0, "mix": 0.75})
            if "clean" in prompt:
                distortion["mix"] = 0.0

        if any(
            word in prompt
            for word in ("punchy", "tight", "glued", "compressed", "controlled", "consistent", "squashed")
        ):
            for item in chain:
                if item["effect_type"] != "compressor":
                    continue
                chain_values[item["instance_id"]].update(
                    {"threshold_db": -30.0, "ratio": 6.0}
                )

        if any(word in prompt for word in ("spacious", "hall", "room", "ambient", "dreamy", "distant", "wash", "reverb")):
            for item in chain:
                if item["effect_type"] == "reverb":
                    chain_values[item["instance_id"]].update({"mix": 0.5, "decay": 0.7})

        parameters = parameter_entries(chain, chain_values)
        diagnostics = "mock profile words: %s" % (", ".join(matched) if matched else "none")
        return (
            parameters,
            {"deterministic": True, "matched_profile_words": matched},
            ["Mock parameters are for integration testing and are not ML inference."],
            diagnostics,
        )
