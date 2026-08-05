"""Restricted canonical JSON and SHA-256 helpers."""

from __future__ import annotations

import hashlib
import json
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import Any


class CanonicalizationError(ValueError):
    """Raised when a value is outside diskann-artifact-c14n-v1."""


def _reject_duplicate_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CanonicalizationError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _parse_input(value: Any) -> Any:
    if isinstance(value, bytes):
        try:
            value = value.decode("utf-8")
        except UnicodeDecodeError as error:
            raise CanonicalizationError("canonical JSON is not valid UTF-8") from error
    if isinstance(value, str):
        try:
            return json.loads(value, object_pairs_hook=_reject_duplicate_pairs)
        except CanonicalizationError:
            raise
        except (json.JSONDecodeError, UnicodeError) as error:
            raise CanonicalizationError("invalid canonical JSON input") from error
    return value


def _validate(value: Any) -> None:
    if value is None or isinstance(value, float):
        raise CanonicalizationError("null and floating-point values are forbidden")
    if isinstance(value, (str, bool, int)):
        return
    if isinstance(value, Mapping):
        for key, item in value.items():
            if not isinstance(key, str):
                raise CanonicalizationError("canonical JSON object keys must be strings")
            _validate(item)
        return
    if isinstance(value, Sequence) and not isinstance(value, (str, bytes, bytearray)):
        for item in value:
            _validate(item)
        return
    raise CanonicalizationError(f"unsupported canonical JSON type: {type(value).__name__}")


def canonicalize_restricted_json(value: Any) -> bytes:
    parsed = _parse_input(value)
    _validate(parsed)
    try:
        return json.dumps(
            parsed,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        ).encode("utf-8")
    except (TypeError, ValueError, UnicodeError) as error:
        raise CanonicalizationError("cannot serialize canonical JSON") from error


def sha256_bytes(value: bytes | bytearray | memoryview) -> str:
    return hashlib.sha256(bytes(value)).hexdigest()


def sha256_file(path: str | Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def aggregate_checksum(projection: Any) -> str:
    return sha256_bytes(canonicalize_restricted_json(projection))

