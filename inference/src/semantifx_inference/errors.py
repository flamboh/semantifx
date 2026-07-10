from typing import Any, Dict, List, Optional


class SemantiFXError(Exception):
    """An expected failure that can be represented on the wire."""

    def __init__(
        self,
        code: str,
        message: str,
        remediation: Optional[List[str]] = None,
        details: Optional[Dict[str, Any]] = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.message = message
        self.remediation = remediation or []
        self.details = details or {}

