from __future__ import annotations

import pytest

from diskann_harness.config import ConfigurationError, load_yaml_text


def test_accepts_ordinary_json_compatible_yaml_tree() -> None:
    result = load_yaml_text(
        """
name: ordinary
items: [one, two]
enabled: true
count: 2
ratio: 1.25
optional: null
"""
    )
    assert result == {
        "name": "ordinary",
        "items": ["one", "two"],
        "enabled": True,
        "count": 2,
        "ratio": 1.25,
        "optional": None,
    }


@pytest.mark.parametrize(
    "payload",
    [
        "duplicate: 1\nduplicate: 2\n",
        "- top\n- level\n",
        "1: non-string-key\n",
        'value: !!python/object/apply:os.system ["echo unsafe"]\n',
        "value: !custom tagged\n",
        "value: 2026-08-05\n",
        'value: !!binary "SGVsbG8="\n',
        "value: !!set {one: null}\n",
        "value: .nan\n",
        "value: .inf\n",
    ],
    ids=[
        "duplicate-key",
        "top-level-sequence",
        "non-string-key",
        "python-object-tag",
        "custom-tag",
        "timestamp",
        "binary",
        "set",
        "nan",
        "infinity",
    ],
)
def test_rejects_unsafe_or_non_json_yaml(payload: str) -> None:
    with pytest.raises(ConfigurationError):
        load_yaml_text(payload)

