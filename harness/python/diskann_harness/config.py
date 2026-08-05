"""Safe YAML loading and normalized RunRequest creation."""

from __future__ import annotations

import copy
import math
from pathlib import Path
from typing import Any

import yaml
from jsonschema import Draft202012Validator, ValidationError
from referencing import Registry, Resource

from .manifests import write_immutable_json
from .workspace import normalize_workspace


class ConfigurationError(ValueError):
    """Stable public error for unsafe or invalid configuration input."""


class DuplicateKeySafeLoader(yaml.SafeLoader):
    """SafeLoader with deterministic duplicate mapping-key rejection."""


def _construct_unique_mapping(
    loader: DuplicateKeySafeLoader, node: yaml.MappingNode, deep: bool = False
) -> dict[str, Any]:
    loader.flatten_mapping(node)
    result: dict[Any, Any] = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        try:
            duplicate = key in result
        except TypeError as error:
            raise ConfigurationError("mapping key is not hashable") from error
        if duplicate:
            raise ConfigurationError(f"duplicate mapping key: {key!r}")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


DuplicateKeySafeLoader.add_constructor(
    yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _construct_unique_mapping
)


def _validate_json_tree(value: Any, location: str = "$", *, top_level: bool = False) -> None:
    if top_level and type(value) is not dict:
        raise ConfigurationError("top-level YAML document must be a mapping")
    if type(value) is dict:
        for key, item in value.items():
            if type(key) is not str:
                raise ConfigurationError(f"mapping key at {location} must be a string")
            _validate_json_tree(item, f"{location}.{key}")
        return
    if type(value) is list:
        for index, item in enumerate(value):
            _validate_json_tree(item, f"{location}[{index}]")
        return
    if type(value) in (str, bool, int) or value is None:
        return
    if type(value) is float and math.isfinite(value):
        return
    raise ConfigurationError(
        f"non-JSON-compatible value at {location}: {type(value).__name__}"
    )


def load_yaml_text(text: str) -> dict[str, Any]:
    try:
        value = yaml.load(text, Loader=DuplicateKeySafeLoader)
    except ConfigurationError:
        raise
    except yaml.YAMLError as error:
        raise ConfigurationError("unsafe or invalid YAML") from error
    _validate_json_tree(value, top_level=True)
    return value


def load_yaml_file(path: str | Path) -> dict[str, Any]:
    try:
        text = Path(path).read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise ConfigurationError(f"cannot read UTF-8 YAML: {path}") from error
    return load_yaml_text(text)


def _schema_validator(schema_root: Path) -> Draft202012Validator:
    schemas = {
        path.name: yaml.safe_load(path.read_text(encoding="utf-8"))
        for path in sorted(schema_root.glob("*.json"))
    }
    registry = Registry().with_resources(
        (schema["$id"], Resource.from_contents(schema))
        for schema in schemas.values()
        if "$id" in schema
    )
    return Draft202012Validator(schemas["run-request.schema.json"], registry=registry)


def normalize_run_request(
    configuration: dict[str, Any],
    *,
    repository_root: str | Path | None = None,
) -> dict[str, Any]:
    normalized = copy.deepcopy(configuration)
    workspace = normalized.get("workspace")
    if type(workspace) is not dict:
        raise ConfigurationError("workspace must be a mapping")
    root = workspace.get("root")
    if type(root) is not str or not root:
        raise ConfigurationError("workspace.root must be a non-empty string")
    memory_budget = workspace.get("memory_budget_bytes")
    thread_budget = workspace.get("thread_budget")
    normalized["workspace"] = {
        **normalize_workspace(root),
        "memory_budget_bytes": memory_budget,
        "thread_budget": thread_budget,
    }

    root_path = (
        Path(repository_root)
        if repository_root is not None
        else Path(__file__).resolve().parents[3]
    )
    schema_root = root_path / "contracts" / "v1" / "schemas"
    try:
        _schema_validator(schema_root).validate(normalized)
    except (ValidationError, KeyError, OSError, yaml.YAMLError) as error:
        raise ConfigurationError(f"RunRequest schema validation failed: {error}") from error
    return normalized


def write_normalized_request(
    path: str | Path, configuration: dict[str, Any]
) -> Path:
    return write_immutable_json(path, configuration)

