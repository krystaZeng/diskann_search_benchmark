"""Immutable JSON document publication primitives."""

from __future__ import annotations

import json
import os
import tempfile
from pathlib import Path
from typing import Any


class ImmutableWriteError(RuntimeError):
    """Raised when immutable publication cannot complete without overwrite."""


def write_immutable_json(path: str | Path, document: Any) -> Path:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(
        document,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{target.name}.", suffix=".tmp", dir=target.parent
    )
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        try:
            os.link(temporary, target)
        except FileExistsError as error:
            raise ImmutableWriteError(f"immutable target already exists: {target}") from error
        except OSError as error:
            raise ImmutableWriteError(f"cannot publish immutable target: {target}") from error
    finally:
        temporary.unlink(missing_ok=True)
    return target

