"""Workspace normalization and confinement for Feature 001."""

from __future__ import annotations

from pathlib import Path


class WorkspaceError(ValueError):
    """Raised when a workspace path escapes its declared root."""


def normalize_workspace(root: str | Path) -> dict[str, str]:
    normalized = Path(root).expanduser().resolve(strict=False)
    if not str(normalized):
        raise WorkspaceError("workspace root is empty")
    return {
        "root": str(normalized),
        "normal_root": str(normalized / "normal"),
        "temporary_root": str(normalized / "temporary"),
        "manifest_root": str(normalized / "manifests"),
        "report_root": str(normalized / "reports"),
        "quarantine_root": str(normalized / "quarantine"),
    }


def resolve_confined_path(root: str | Path, relative_path: str | Path) -> Path:
    normalized_root = Path(root).expanduser().resolve(strict=False)
    relative = Path(relative_path)
    if relative.is_absolute() or not relative.parts or ".." in relative.parts:
        raise WorkspaceError("path must be non-empty, relative, and contain no '..'")
    candidate = (normalized_root / relative).resolve(strict=False)
    try:
        candidate.relative_to(normalized_root)
    except ValueError as error:
        raise WorkspaceError("path escapes workspace root") from error
    if candidate == normalized_root:
        raise WorkspaceError("path must resolve below workspace root")
    return candidate


def create_workspace(roots: dict[str, str]) -> None:
    root = Path(roots["root"])
    for key in (
        "normal_root",
        "temporary_root",
        "manifest_root",
        "report_root",
        "quarantine_root",
    ):
        path = Path(roots[key]).resolve(strict=False)
        try:
            path.relative_to(root)
        except ValueError as error:
            raise WorkspaceError(f"{key} escapes workspace root") from error
        path.mkdir(parents=True, exist_ok=True)

