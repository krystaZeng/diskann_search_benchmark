from __future__ import annotations

import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build/feature-001"


def test_temporary_install_contains_only_production_assembly(tmp_path: Path) -> None:
    prefix = tmp_path / "install"
    completed = subprocess.run(
        ["cmake", "--install", str(BUILD), "--prefix", str(prefix)],
        capture_output=True,
        text=True,
        shell=False,
    )
    assert completed.returncode == 0, completed.stderr
    installed = [path.relative_to(prefix).as_posix() for path in prefix.rglob("*")]
    assert any(path.endswith("bin/diskann_pipeline_runner") for path in installed)
    assert not any("fake_runner" in path for path in installed)
    assert not any("fake_stages" in path for path in installed)
    assert not any("fake_pipeline" in path for path in installed)
    assert not any("fixture" in path.lower() for path in installed)

