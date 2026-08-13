import json
import os
from pathlib import Path
from typing import Callable, List, Optional


ProgressCallback = Callable[[str, int, int, Optional[List[dict]]], None]


class AnalysisCancelled(RuntimeError):
    """Stops one analysis without terminating its persistent worker."""


class ProgressFileWriter:
    """Atomically publishes the latest analysis progress snapshot."""

    def __init__(self, destination: Path, request_id: Optional[str] = None) -> None:
        self.destination = Path(destination)
        self.request_id = request_id

    def __call__(
        self,
        phase: str,
        step: int,
        total_steps: int,
        parameters: Optional[List[dict]] = None,
    ) -> None:
        snapshot = {
            "protocol_version": "1",
            "type": "analysis_progress",
            "phase": phase,
            "step": step,
            "total_steps": total_steps,
        }
        if self.request_id is not None:
            snapshot["request_id"] = self.request_id
        if parameters is not None:
            snapshot["parameters"] = parameters

        temporary = self.destination.with_name(self.destination.name + ".tmp")
        try:
            self.destination.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps(snapshot, separators=(",", ":")), encoding="utf-8")
            os.replace(temporary, self.destination)
        except OSError:
            # Progress is advisory; losing it must never fail an analysis.
            try:
                temporary.unlink(missing_ok=True)
            except OSError:
                pass
