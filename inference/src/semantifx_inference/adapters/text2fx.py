import contextlib
import io
import os
from pathlib import Path
import random
import sys
import threading
from typing import Any, Dict, List, Tuple

from ..errors import SemantiFXError


_MODEL_CACHE = {}
_MODEL_CACHE_LOCK = threading.Lock()
_MODEL_CONTEXT = threading.local()


def _seed_model_runtime(text2fx_main, seed):
    if seed is None:
        return
    random.seed(seed)
    text2fx_main.np.random.seed(seed)
    text2fx_main.torch.manual_seed(seed)
    if text2fx_main.torch.cuda.is_available():
        text2fx_main.torch.cuda.manual_seed_all(seed)


def _install_model_cache(text2fx_main):
    """Patch Text2FX's model factory with a process-local caching wrapper.

    Text2FX's optimization function resolves ``get_model`` from its own module
    globals on every call. Replacing that one factory preserves the sibling
    package while allowing a persistent JSONL worker to reuse loaded CLAP
    weights. The wrapper is idempotent because adapters are short-lived.
    """
    current = text2fx_main.get_model
    if getattr(current, "_semantifx_model_cache", False):
        return current

    original = current

    def cached_get_model(model_choice):
        key = (original, model_choice)
        with _MODEL_CACHE_LOCK:
            if key in _MODEL_CACHE:
                cached_get_model.cache_hits += 1
                model = _MODEL_CACHE[key]
            else:
                cached_get_model.cache_misses += 1
                model = original(model_choice)
                _MODEL_CACHE[key] = model
        # Model construction can consume RNG state. Seed immediately after the
        # factory returns so cold and warm requests start optimization equally.
        _seed_model_runtime(text2fx_main, getattr(_MODEL_CONTEXT, "random_seed", None))
        return model

    cached_get_model._semantifx_model_cache = True
    cached_get_model.cache_hits = 0
    cached_get_model.cache_misses = 0
    text2fx_main.get_model = cached_get_model
    return cached_get_model


def _default_repo() -> Path:
    # .../semantifx/inference/src/semantifx_inference/adapters/text2fx.py
    semantifx_repo = Path(__file__).resolve().parents[4]
    return semantifx_repo.parent / "oss" / "text2fx"


def _plain_value(value: Any) -> Any:
    if hasattr(value, "detach"):
        value = value.detach().cpu()
    if hasattr(value, "tolist"):
        value = value.tolist()
    while isinstance(value, (list, tuple)) and len(value) == 1:
        value = value[0]
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise SemantiFXError(
            "invalid_backend_result",
            "Text2FX returned a non-scalar parameter value.",
            ["Run one audio file and prompt per request."],
            {"value_type": type(value).__name__},
        )
    return float(value)


def _unit(name: str) -> str:
    if name.endswith("_gain_db") or name.endswith("_db"):
        return "dB"
    if name.endswith("_freq") or name.endswith("_hz"):
        return "Hz"
    if name.endswith("_q_factor") or name.endswith("_ratio"):
        return "ratio"
    return "unitless"


def _canonical_effect(name: str) -> str:
    compact = name.lower().replace("_", "")
    if compact in ("parametriceq", "parametriceq40band"):
        return "eq"
    return name


class Text2FXAdapter:
    name = "text2fx"

    def __init__(self, repo: Path = None) -> None:
        configured = os.environ.get("SEMANTIFX_TEXT2FX_REPO")
        self.repo = Path(configured).expanduser() if configured else (repo or _default_repo())

    def _load(self):
        package_dir = self.repo / "text2fx"
        if not package_dir.is_dir():
            raise SemantiFXError(
                "text2fx_checkout_not_found",
                "The Text2FX checkout was not found at %s." % self.repo,
                ["Set SEMANTIFX_TEXT2FX_REPO to the Text2FX repository path."],
                {"repo": str(self.repo)},
            )
        repo_string = str(self.repo)
        if repo_string not in sys.path:
            sys.path.insert(0, repo_string)
        try:
            from text2fx.apply import main as apply_text2fx
            import text2fx.__main__ as text2fx_main
        except (ImportError, ModuleNotFoundError) as exc:
            missing = getattr(exc, "name", None) or str(exc)
            raise SemantiFXError(
                "text2fx_dependency_unavailable",
                "Text2FX could not be imported because a dependency is unavailable: %s." % missing,
                [
                    "Use a Python 3.9 environment.",
                    "Install the sibling checkout and its requirements in that environment.",
                    "See inference/README.md for an invocation using the existing Text2FX environment.",
                ],
                {"repo": str(self.repo), "missing_module": missing, "python": sys.executable},
            ) from exc
        return apply_text2fx, _install_model_cache(text2fx_main)

    def analyze(self, request: dict) -> Tuple[List[dict], Dict[str, object], List[str], str]:
        audio_path = Path(request["audio"]["path"]).expanduser()
        if not audio_path.is_file():
            raise SemantiFXError(
                "audio_not_found",
                "The input audio file does not exist: %s." % audio_path,
                ["Capture or write the excerpt before submitting the analysis request."],
                {"path": str(audio_path)},
            )

        options = request.get("options", {})
        stdout_capture = io.StringIO()
        stderr_capture = io.StringIO()
        try:
            with contextlib.redirect_stdout(stdout_capture), contextlib.redirect_stderr(stderr_capture):
                apply_text2fx, model_loader = self._load()
                cache_hits_before = model_loader.cache_hits
                cache_misses_before = model_loader.cache_misses
                _MODEL_CONTEXT.random_seed = options.get("random_seed")
                try:
                    _signal, _raw_parameters, parameter_map = apply_text2fx(
                        audio_path=audio_path,
                        fx_chain=request["effects"],
                        text_target=request["prompt"],
                        learning_rate=options.get("learning_rate", 0.01),
                        params_init_type=options.get("parameter_initialization", "zeros"),
                        param_reg_weight=options.get("parameter_regularization", 0.0),
                        roll_amt=options.get("roll_samples"),
                        n_iters=options.get("iterations", 100),
                        criterion=options.get("criterion", "cosine-sim"),
                        model=options.get("model", "ms_clap"),
                    )
                finally:
                    del _MODEL_CONTEXT.random_seed
        except SemantiFXError:
            raise
        except Exception as exc:
            raise SemantiFXError(
                "text2fx_execution_failed",
                "Text2FX analysis failed: %s" % exc,
                [
                    "Run the same request with --diagnostics to inspect captured Text2FX output.",
                    "Confirm model weights and Text2FX dependencies are available to this Python interpreter.",
                ],
                {
                    "exception_type": type(exc).__name__,
                    "python": sys.executable,
                    "repo": str(self.repo),
                    "captured_stdout": stdout_capture.getvalue()[-4000:],
                    "captured_stderr": stderr_capture.getvalue()[-4000:],
                },
            ) from exc

        parameters = []
        for effect_name, effect_parameters in parameter_map.items():
            if not isinstance(effect_parameters, dict):
                raise SemantiFXError(
                    "invalid_backend_result",
                    "Text2FX returned an invalid parameter map.",
                    ["Check the Text2FX adapter against the current sibling checkout."],
                    {"effect": effect_name, "value_type": type(effect_parameters).__name__},
                )
            for name, value in effect_parameters.items():
                parameters.append(
                    {
                        "effect": _canonical_effect(effect_name),
                        "name": name,
                        "value": _plain_value(value),
                        "unit": _unit(name),
                    }
                )
        diagnostics = stdout_capture.getvalue() + stderr_capture.getvalue()
        return (
            parameters,
            {
                "model": options.get("model", "ms_clap"),
                "criterion": options.get("criterion", "cosine-sim"),
                "iterations": options.get("iterations", 100),
                "random_seed": options.get("random_seed"),
                "model_cache_hit": model_loader.cache_hits > cache_hits_before,
                "model_cache_miss": model_loader.cache_misses > cache_misses_before,
            },
            [],
            diagnostics,
        )
