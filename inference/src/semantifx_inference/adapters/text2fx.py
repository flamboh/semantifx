import contextlib
import io
import os
from pathlib import Path
import sys
from typing import Any, Dict, List, Tuple

from ..errors import SemantiFXError


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
        return apply_text2fx

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
                apply_text2fx = self._load()
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
            },
            [],
            diagnostics,
        )
