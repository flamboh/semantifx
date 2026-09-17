"""PROTOTYPE. Throwaway. Not imported by the package and never will be.

Question (wayfinder ticket #4, part of #1): what does the optimizer look like
when it builds its differentiable chain from the protocol's chain list instead
of the fixed ``EqDistortionChain``?

This file answers by construction:

1. ``CATALOGUE`` is ONE registry keyed by effect type. Each entry carries the
   parameter specs (the same tuples ``scripts/gen`` emits today), the Jev
   description assembly uses, and a ``render`` function. Assembly and the
   optimizer read the same object.
2. ``VariableChain`` is a ``torch.nn.Module`` built from any chain list. Each
   instance owns a slice of one flat normalized tensor; identity is the
   ``instance_id`` from the request, type comes from ``effect_type``.
3. The scenarios at the bottom push the module through the REAL ``CLAPAdapter``
   loop with only its chain class swapped, plus a bit-for-bit parity check
   against ``EqDistortionChain`` on the fixed pair.

Run from ``inference/``::

    PYTHONPATH=src .venv/bin/python diagnostics/proto_variable_chain.py \
        [--audio path.wav] [--iterations 5] [--skip-clap]

Prints every scenario's layout, gradient norms per instance, and the result
snapshot so the shape can be judged. Nothing here is production quality.
"""

from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
import tempfile
import time
from typing import Callable, Dict, List, Sequence

import dasp_pytorch
import torch

from semantifx_inference._generated_effects import EFFECTS
from semantifx_inference.chain_schema import ParameterSpec
from semantifx_inference.distortion import process as process_distortion


# --------------------------------------------------------------------------
# 1. One catalogue, keyed by effect type.
# --------------------------------------------------------------------------

Render = Callable[[torch.Tensor, float, Dict[str, torch.Tensor]], torch.Tensor]


@dataclass(frozen=True)
class EffectDefinition:
    effect_type: str
    description: str  # what Jev sees in state
    parameters: Sequence[ParameterSpec]  # what the protocol and plugin see
    render: Render  # what the optimizer runs


def _specs_from_generated(effect_type: str) -> Sequence[ParameterSpec]:
    for _instance_id, generated_type, parameters in EFFECTS:
        if generated_type == effect_type:
            return tuple(ParameterSpec(*entry) for entry in parameters)
    raise KeyError(effect_type)


def _render_eq(audio: torch.Tensor, sample_rate: float, p: Dict[str, torch.Tensor]) -> torch.Tensor:
    # dasp's ParametricEQ module is stateless: process_normalized is just
    # denormalize + this call. Calling the functional form with physical values
    # means one shared code path for every EQ instance, no module per instance.
    return dasp_pytorch.functional.parametric_eq(audio, sample_rate, **p)


def _render_distortion(audio: torch.Tensor, sample_rate: float, p: Dict[str, torch.Tensor]) -> torch.Tensor:
    return process_distortion(audio, sample_rate, **p)


def _render_compressor(audio: torch.Tensor, sample_rate: float, p: Dict[str, torch.Tensor]) -> torch.Tensor:
    # Trimmed dasp compressor per ticket #6: no release parameter (dasp never
    # uses it), knee floored above zero. release_ms is passed as a constant
    # because the dasp signature still requires it.
    return dasp_pytorch.functional.compressor(
        audio,
        sample_rate,
        threshold_db=p["threshold_db"],
        ratio=p["ratio"],
        attack_ms=p["attack_ms"],
        release_ms=torch.full_like(p["attack_ms"], 50.0),
        knee_db=p["knee_db"],
        makeup_gain_db=p["makeup_gain_db"],
    )


CATALOGUE: Dict[str, EffectDefinition] = {
    "eq": EffectDefinition(
        "eq",
        "Six-band parametric equalizer with a low shelf, four peaking bands, and a high shelf. "
        "Boosts or cuts frequency ranges to shape brightness, warmth, body, presence, harshness, mud, thinness, and clarity.",
        _specs_from_generated("eq"),
        _render_eq,
    ),
    "distortion": EffectDefinition(
        "distortion",
        "Distortion with drive, character, tone, and wet/dry mix. Nonlinear waveshaping that adds harmonics "
        "for saturation, grit, warmth from soft clipping, overdrive, fuzz, and aggression.",
        _specs_from_generated("distortion"),
        _render_distortion,
    ),
    # Illustrative third type: shows that a new effect is one registry entry.
    # Ranges are dasp's with the knee floored (ticket #6). Defaults are NOT a
    # decision; ticket #11 owns initialization.
    "compressor": EffectDefinition(
        "compressor",
        "Dynamic range compressor with threshold, ratio, attack, soft knee, and makeup gain. Evens out level "
        "differences to add punch, tightness, sustain, glue, consistency, and perceived loudness, or to tame "
        "peaks and control dynamics.",
        (
            ParameterSpec("threshold_db", "dB", -60.0, 0.0, -20.0),
            ParameterSpec("ratio", "ratio", 1.0, 20.0, 4.0),
            ParameterSpec("attack_ms", "ms", 5.0, 100.0, 20.0),
            ParameterSpec("knee_db", "dB", 0.5, 12.0, 6.0),
            ParameterSpec("makeup_gain_db", "dB", 0.0, 12.0, 0.0),
        ),
        _render_compressor,
    ),
}


# --------------------------------------------------------------------------
# 2. A chain module built from any chain list.
# --------------------------------------------------------------------------


@dataclass(frozen=True)
class Slot:
    instance_id: str
    definition: EffectDefinition
    offset: int

    @property
    def width(self) -> int:
        return len(self.definition.parameters)


def validate_chain(chain: List[dict], catalogue: Dict[str, EffectDefinition]) -> List[Slot]:
    """Structural validation the protocol layer would do; returns the layout."""
    if not isinstance(chain, list):
        raise ValueError("chain must be a list")
    seen = set()
    slots: List[Slot] = []
    offset = 0
    for index, item in enumerate(chain):
        where = f"chain[{index}]"
        instance_id = item.get("instance_id")
        if not isinstance(instance_id, str) or not instance_id:
            raise ValueError(f"{where}.instance_id must be a non-empty string")
        if instance_id in seen:
            raise ValueError(f"{where}.instance_id {instance_id!r} is duplicated")
        seen.add(instance_id)
        effect_type = item.get("effect_type")
        if effect_type not in catalogue:
            raise ValueError(f"{where}.effect_type {effect_type!r} is not in the catalogue")
        definition = catalogue[effect_type]
        expected = {spec.name for spec in definition.parameters}
        parameters = item.get("parameters")
        if not isinstance(parameters, dict) or set(parameters) != expected:
            raise ValueError(f"{where}.parameters must contain exactly {sorted(expected)}")
        for spec in definition.parameters:
            value = parameters[spec.name]
            if not spec.minimum <= value <= spec.maximum:
                raise ValueError(f"{where}.parameters.{spec.name} out of range")
        slots.append(Slot(instance_id, definition, offset))
        offset += len(definition.parameters)
    return slots


class VariableChain(torch.nn.Module):
    """Same surface as EqDistortionChain: normalized_configuration, forward, snapshot."""

    def __init__(self, sample_rate: float, chain: List[dict], catalogue: Dict[str, EffectDefinition] = CATALOGUE) -> None:
        super().__init__()
        self.sample_rate = sample_rate
        self.layout = validate_chain(chain, catalogue)
        self.num_params = sum(slot.width for slot in self.layout)

    def normalized_configuration(self, chain: List[dict], device=None) -> torch.Tensor:
        by_id = {item["instance_id"]: item["parameters"] for item in chain}
        values = []
        for slot in self.layout:
            for spec in slot.definition.parameters:
                value = float(by_id[slot.instance_id][spec.name])
                values.append((value - spec.minimum) / (spec.maximum - spec.minimum))
        return torch.tensor([values], dtype=torch.float32, device=device)

    def physical(self, normalized: torch.Tensor, slot: Slot) -> Dict[str, torch.Tensor]:
        return {
            spec.name: spec.minimum + normalized[:, slot.offset + i] * (spec.maximum - spec.minimum)
            for i, spec in enumerate(slot.definition.parameters)
        }

    def forward(self, signal, normalized: torch.Tensor):
        normalized = normalized.clamp(0.0, 1.0)
        output = signal.clone()
        audio = output.audio_data
        for slot in self.layout:  # request order IS render order
            audio = slot.definition.render(audio, self.sample_rate, self.physical(normalized, slot))
        output.audio_data = audio
        return output

    def snapshot(self, normalized: torch.Tensor) -> List[dict]:
        normalized = normalized.detach().cpu().clamp(0.0, 1.0)
        entries = []
        for slot in self.layout:  # result order follows the chain, not a canonical table
            # Denormalize in Python floats exactly as EqDistortionChain.snapshot
            # does, so the golden fixture stays byte-identical after migration.
            entries.extend(
                {
                    "effect_instance_id": slot.instance_id,
                    "effect": slot.definition.effect_type,
                    "name": spec.name,
                    "value": float(spec.minimum + normalized[0, slot.offset + i].item() * (spec.maximum - spec.minimum)),
                    "unit": spec.unit,
                }
                for i, spec in enumerate(slot.definition.parameters)
            )
        return entries

    def grad_norms(self, normalized: torch.Tensor) -> Dict[str, float]:
        grad = normalized.grad
        if grad is None:
            return {}
        return {
            slot.instance_id: float(grad[0, slot.offset : slot.offset + slot.width].norm().item())
            for slot in self.layout
        }


def default_chain_from_types(types: Sequence[str], catalogue=CATALOGUE) -> List[dict]:
    """Assembly's to_chain, rewritten against the catalogue: ids are <type>-<n>."""
    counts: Dict[str, int] = {}
    chain = []
    for effect_type in types:
        counts[effect_type] = counts.get(effect_type, 0) + 1
        definition = catalogue[effect_type]
        chain.append(
            {
                "instance_id": f"{effect_type}-{counts[effect_type]}",
                "effect_type": effect_type,
                "parameters": {spec.name: spec.default for spec in definition.parameters},
            }
        )
    return chain


# --------------------------------------------------------------------------
# 3. Scenarios.
# --------------------------------------------------------------------------


class _Signal:
    def __init__(self, audio_data: torch.Tensor) -> None:
        self.audio_data = audio_data

    def clone(self):
        return _Signal(self.audio_data.clone())


def _banner(title: str) -> None:
    print("\n" + "=" * 78 + f"\n{title}\n" + "=" * 78)


def _describe(chain_module: VariableChain) -> None:
    print("layout:")
    for slot in chain_module.layout:
        print(f"  {slot.instance_id:<14} type={slot.definition.effect_type:<11} offset={slot.offset:<3} width={slot.width}")
    print(f"num_params={chain_module.num_params}")


def scenario_parity_with_fixed_chain() -> None:
    """The variable chain must render the fixed pair identically to today's module."""
    from semantifx_inference.chain_schema import DISTORTION_THEN_EQ, default_chain
    from semantifx_inference.differentiable_chain import EqDistortionChain

    _banner("Parity: VariableChain vs EqDistortionChain on the fixed pair, both orders")
    torch.manual_seed(0)
    sample_rate = 44_100.0
    audio = torch.randn(1, 1, 2 * 44_100) * 0.3
    for order_name, chain in (("eq->distortion", default_chain()), ("distortion->eq", default_chain(DISTORTION_THEN_EQ))):
        by_id = {item["instance_id"]: item for item in chain}
        by_id["eq-1"]["parameters"].update({"low_shelf_gain_db": 9.0, "band1_gain_db": -6.0, "high_shelf_gain_db": 4.0})
        by_id["distortion-1"]["parameters"].update({"drive_db": 30.0, "character": 0.8, "tone_db": 3.0, "mix": 0.9})
        fixed = EqDistortionChain(sample_rate, chain)
        variable = VariableChain(sample_rate, chain)
        # Fixed module always stores eq first then distortion; the variable
        # module stores in chain order. Compare renders from each module's
        # own normalized layout so the test is about audio, not tensor layout.
        out_fixed = fixed(_Signal(audio), fixed.normalized_configuration(chain)).audio_data
        out_variable = variable(_Signal(audio), variable.normalized_configuration(chain)).audio_data
        diff = (out_fixed - out_variable).abs().max().item()
        print(f"{order_name:<16} max |diff| = {diff:.3e}  {'OK' if diff == 0.0 else 'MISMATCH'}")
        snap_fixed = {(e['effect_instance_id'], e['name']): e['value'] for e in fixed.snapshot(fixed.normalized_configuration(chain))}
        snap_variable = {(e['effect_instance_id'], e['name']): e['value'] for e in variable.snapshot(variable.normalized_configuration(chain))}
        print(f"{'':<16} snapshot values equal: {snap_fixed == snap_variable}; "
              f"fixed order={[e['effect_instance_id'] for e in fixed.snapshot(fixed.normalized_configuration(chain))][::18][:2]}, "
              f"variable order={[e['effect_instance_id'] for e in variable.snapshot(variable.normalized_configuration(chain))][::18][:2]}")


def scenario_structure_and_gradients() -> None:
    """No CLAP: build odd chains, run one backward pass with a toy loss."""
    _banner("Structure + gradients (toy loss, no CLAP)")
    torch.manual_seed(0)
    sample_rate = 44_100.0
    audio = torch.randn(1, 1, 44_100) * 0.3
    cases = {
        "eq only": ["eq"],
        "distortion, eq": ["distortion", "eq"],
        "eq, distortion, eq (repeat)": ["eq", "distortion", "eq"],
        "compressor, eq (new type)": ["compressor", "eq"],
        "eq x3": ["eq", "eq", "eq"],
        "empty": [],
    }
    for title, types in cases.items():
        print(f"\n--- {title}")
        chain = default_chain_from_types(types)
        # Give repeats different starting points so their identities are visible.
        for n, item in enumerate(chain):
            if item["effect_type"] == "eq":
                item["parameters"]["band1_gain_db"] = 3.0 * (n + 1)
        module = VariableChain(sample_rate, chain)
        _describe(module)
        params = torch.nn.Parameter(module.normalized_configuration(chain))
        out = module(_Signal(audio), params).audio_data
        loss = (out.square().mean() - 0.5).abs()  # arbitrary, just needs a gradient
        if module.num_params:
            loss.backward()
        else:
            print("no parameters: loss.backward() would raise 'does not require grad'; the worker must short-circuit an empty chain")
        print(f"grad norm per instance: {json.dumps(module.grad_norms(params))}")
        print("snapshot ids in order:", list(dict.fromkeys(e["effect_instance_id"] for e in module.snapshot(params))))
        if title == "empty":
            print(f"empty chain -> output is input: {torch.equal(out, audio)}; num_params={module.num_params}; snapshot={module.snapshot(params)}")

    print("\n--- structural rejections")
    bad = [
        ("duplicate id", default_chain_from_types(["eq"]) * 2),
        ("unknown type", [{"instance_id": "delay-1", "effect_type": "delay", "parameters": {}}]),
        ("missing params", [{"instance_id": "eq-1", "effect_type": "eq", "parameters": {"low_shelf_gain_db": 0.0}}]),
        ("id/type mismatch is allowed (id is opaque)", [{"instance_id": "verb-9", "effect_type": "distortion", "parameters": default_chain_from_types(["distortion"])[0]["parameters"]}]),
    ]
    for title, chain in bad:
        try:
            VariableChain(sample_rate, chain)
            print(f"{title}: accepted")
        except ValueError as exc:
            print(f"{title}: rejected -> {exc}")


def scenario_real_optimizer(audio_path: Path, iterations: int) -> None:
    """Push variable chains through the real CLAPAdapter with only its chain class swapped."""
    from semantifx_inference.adapters.clap import CLAPAdapter

    _banner(f"Real CLAP optimizer loop, {iterations} iterations each, audio={audio_path.name}")
    adapter = CLAPAdapter()
    original_load = adapter._load

    def patched_load():
        runtime, _fixed_chain_type, model_loader = original_load()
        return runtime, VariableChain, model_loader  # the ONLY change to the worker loop

    adapter._load = patched_load

    cases = {
        "eq only, 'brighter'": (["eq"], "brighter"),
        "eq, distortion, eq: 'warm and gritty'": (["eq", "distortion", "eq"], "warm and gritty"),
        "compressor, eq: 'punchy'": (["compressor", "eq"], "punchy"),
        "empty chain, 'brighter'": ([], "brighter"),
    }
    for title, (types, prompt) in cases.items():
        print(f"\n--- {title}")
        chain = default_chain_from_types(types)
        for item in chain:
            if item["effect_type"] == "eq":
                item["parameters"]["band2_gain_db"] = 1.0  # off the 0 dB zero-gradient point
        request = {
            "type": "analyze",
            "request_id": "proto",
            "prompt": prompt,
            "audio": {"path": str(audio_path)},
            "chain": chain,
            "options": {"backend": "clap", "iterations": iterations, "random_seed": 0},
        }
        started = time.perf_counter()
        try:
            parameters, metrics, warnings, _diag = adapter.analyze(request)
        except Exception as exc:  # show what the worker would report
            print(f"FAILED: {type(exc).__name__}: {getattr(exc, 'message', exc)}")
            details = getattr(exc, "details", {})
            if details.get("captured_stderr"):
                print(details["captured_stderr"][-600:])
            continue
        elapsed = time.perf_counter() - started
        before = {(i["instance_id"], k): v for i in chain for k, v in i["parameters"].items()}
        moved = sorted(
            ((e["effect_instance_id"], e["name"], before[(e["effect_instance_id"], e["name"])], e["value"]) for e in parameters),
            key=lambda t: -abs(t[3] - t[2]),
        )[:5]
        print(f"ok in {elapsed:.1f}s; {len(parameters)} parameter entries; order={list(dict.fromkeys(e['effect_instance_id'] for e in parameters))}")
        print("largest moves:")
        for instance_id, name, b, a in moved:
            print(f"  {instance_id:<14}{name:<24}{b:>10.3f} -> {a:>10.3f}")
        print("metrics:", {k: metrics[k] for k in ("iterations", "model_cache_hit")})


def _cropped_copy(audio_path: Path, seconds: float) -> Path:
    import soundfile as sf

    data, sample_rate = sf.read(audio_path)
    if len(data) <= seconds * sample_rate:
        return audio_path
    out = Path(tempfile.gettempdir()) / "semantifx-proto-variable-chain-crop.wav"
    sf.write(out, data[: int(seconds * sample_rate)], sample_rate)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--audio", default=str(Path.home() / "code/semantifx/web/public/original-guitar.wav"))
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--seconds", type=float, default=5.0)
    parser.add_argument("--skip-clap", action="store_true")
    args = parser.parse_args()

    scenario_parity_with_fixed_chain()
    scenario_structure_and_gradients()
    if not args.skip_clap:
        scenario_real_optimizer(_cropped_copy(Path(args.audio).expanduser(), args.seconds), args.iterations)


if __name__ == "__main__":
    main()
