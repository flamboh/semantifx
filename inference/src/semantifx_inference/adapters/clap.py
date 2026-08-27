import contextlib
import io
from pathlib import Path
import sys
from typing import Dict, List, Optional, Tuple

from ..errors import SemantiFXError
from ..model_runtime import install_model_cache
from ..progress import AnalysisCancelled, ProgressCallback


def load_optimization_audio(runtime, audio_path: Path):
    """Load/resample a capture without erasing its level-dependent dynamics."""
    return (
        runtime.AudioSignal(audio_path)
        .to_mono()
        .resample(runtime.SAMPLE_RATE)
        .to(runtime.device)
    )


class CLAPAdapter:
    """Optimize the SemantiFX-owned fixed chain using a cached CLAP model."""

    name = "clap"

    def __init__(self, progress: Optional[ProgressCallback] = None) -> None:
        self.progress = progress

    def _load(self):
        try:
            from .. import text2fx_vendor as runtime
            from ..differentiable_chain import EqDistortionChain
        except (ImportError, ModuleNotFoundError) as exc:
            missing = getattr(exc, "name", None) or str(exc)
            raise SemantiFXError(
                "clap_dependency_unavailable",
                "The local CLAP optimizer could not load a dependency: %s." % missing,
                ["Install the pinned inference dependencies documented in inference/README.md."],
                {"missing_module": missing, "python": sys.executable},
            ) from exc
        return runtime, EqDistortionChain, install_model_cache(runtime)

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
        total_steps = options.get("iterations", 100)
        if self.progress is not None:
            self.progress("loading_model", 0, total_steps, None)

        stdout_capture = io.StringIO()
        stderr_capture = io.StringIO()
        try:
            with contextlib.redirect_stdout(stdout_capture), contextlib.redirect_stderr(stderr_capture):
                runtime, chain_type, model_loader = self._load()
                seed = options.get("random_seed", 0)
                cache_hits_before = model_loader.cache_hits
                cache_misses_before = model_loader.cache_misses
                model = model_loader(options.get("model", "ms_clap"))
                runtime.seed_runtime(seed)

                signal = load_optimization_audio(runtime, audio_path)
                chain = chain_type(runtime.SAMPLE_RATE, request["chain"]).to(runtime.device)
                initial = chain.normalized_configuration(request["chain"], runtime.device)
                if options.get("parameter_initialization", "current") == "random":
                    initial = runtime.torch.rand_like(initial)
                parameters = runtime.torch.nn.Parameter(initial.clone())
                optimizer = runtime.torch.optim.Adam(
                    [parameters], lr=options.get("learning_rate", 0.01)
                )

                target = model.get_text_embeddings(["this sound is " + request["prompt"]]).detach()
                criterion = options.get("criterion", "cosine-sim")
                if criterion == "directional_loss":
                    input_embedding = model.get_audio_embeddings(signal.to(runtime.device)).detach()
                    negative_target = model.get_text_embeddings(
                        ["this sound is not " + request["prompt"]]
                    ).detach()

                for step in range(total_steps):
                    rolled = signal.clone()
                    roll_samples = options.get("roll_samples")
                    if roll_samples is None:
                        amount = runtime.torch.randint(0, rolled.signal_length, (rolled.batch_size,))
                    else:
                        amount = runtime.torch.randint(-roll_samples, roll_samples + 1, (rolled.batch_size,))
                    for batch in range(rolled.batch_size):
                        rolled.samples[batch] = runtime.torch.roll(
                            rolled.samples[batch], shifts=amount[batch].item(), dims=-1
                        )

                    effected = chain(rolled.to(runtime.device), parameters)
                    embedding = model.get_audio_embeddings(effected)
                    if criterion == "directional_loss":
                        batch_loss = runtime.clip_directional_loss(
                            embedding, input_embedding, target, negative_target
                        )
                    elif criterion == "standard":
                        batch_loss = -(embedding @ target.T)
                    else:
                        batch_loss = 1 - runtime.torch.cosine_similarity(embedding, target, dim=-1)
                    loss = batch_loss.mean()
                    regularization = options.get("parameter_regularization", 0.0)
                    if regularization:
                        loss = loss + regularization * (parameters - initial).square().mean()

                    optimizer.zero_grad()
                    loss.backward()
                    optimizer.step()
                    with runtime.torch.no_grad():
                        parameters.clamp_(0.0, 1.0)
                    if self.progress is not None:
                        self.progress("optimizing", step + 1, total_steps, chain.snapshot(parameters))

                result = chain.snapshot(parameters)
                if self.progress is not None:
                    self.progress("finalizing", total_steps, total_steps, result)
        except (AnalysisCancelled, SemantiFXError):
            raise
        except Exception as exc:
            raise SemantiFXError(
                "clap_execution_failed",
                "SemantiFX chain optimization failed: %s" % exc,
                [
                    "Run the same request with diagnostics enabled.",
                    "Confirm model weights and Text2FX dependencies are available.",
                ],
                {
                    "exception_type": type(exc).__name__,
                    "python": sys.executable,
                    "captured_stdout": stdout_capture.getvalue()[-4000:],
                    "captured_stderr": stderr_capture.getvalue()[-4000:],
                },
            ) from exc

        diagnostics = stdout_capture.getvalue() + stderr_capture.getvalue()
        return (
            result,
            {
                "model": options.get("model", "ms_clap"),
                "criterion": options.get("criterion", "cosine-sim"),
                "iterations": total_steps,
                "random_seed": options.get("random_seed", 0),
                "model_cache_hit": model_loader.cache_hits > cache_hits_before,
                "model_cache_miss": model_loader.cache_misses > cache_misses_before,
            },
            ["Distortion currently runs at the session sample rate without oversampling."],
            diagnostics,
        )
