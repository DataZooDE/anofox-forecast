"""Shared provenance + strict-JSON/CSV writers for Python reference-fixture
generators (TEST-01 harness, Phase 09-02).

stdlib + numpy only. No timestamps or hostnames are ever written, so
regeneration is byte-identical given the same package versions and RNG seed.
"""

from __future__ import annotations

import csv
import importlib.metadata
import json
import math
import platform
from pathlib import Path
from typing import Any, Iterable

import numpy as np

# validation/reference/python/fixture_io.py -> parents[0]=python, [1]=reference,
# [2]=validation, [3]=repo root.
REPO_ROOT = Path(__file__).resolve().parents[3]
FIXTURES = REPO_ROOT / "validation" / "reference" / "fixtures"


def generator_path(script_file: str) -> str:
    """Repo-relative POSIX path of the calling script (for fixture provenance)."""
    return Path(script_file).resolve().relative_to(REPO_ROOT).as_posix()


def tool_versions(*dist_names: str) -> dict[str, str]:
    """Dict with the running python version plus the installed version of
    each named distribution."""
    versions: dict[str, str] = {"python": platform.python_version()}
    for name in dist_names:
        versions[name] = importlib.metadata.version(name)
    return versions


def encode(obj: Any) -> Any:
    """Recursively convert numpy types and non-finite floats into JSON-safe
    Python values.

    - numpy arrays -> lists (recursively encoded)
    - numpy scalars -> python int/float
    - non-finite floats (nan/inf/-inf) -> the strings "NaN"/"Infinity"/"-Infinity"
    - dict/list/tuple -> recursed
    - everything else -> returned unchanged
    """
    if isinstance(obj, np.ndarray):
        return [encode(x) for x in obj.tolist()]
    if isinstance(obj, (np.generic,)):
        return encode(obj.item())
    if isinstance(obj, float):
        if math.isnan(obj):
            return "NaN"
        if math.isinf(obj):
            return "Infinity" if obj > 0 else "-Infinity"
        return obj
    if isinstance(obj, dict):
        return {k: encode(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple)):
        return [encode(v) for v in obj]
    return obj


def write_fixture(
    family: str,
    filename: str,
    generator: str,
    versions: dict[str, str],
    payload: dict[str, Any],
) -> Path:
    """Write a fixture JSON file under FIXTURES/family/filename.

    `payload` MUST contain a `tolerance` dict (raises ValueError otherwise).
    The written document is `{family, generator, tool_versions, **payload}`,
    serialized via `json.dumps(encode(obj), indent=2, allow_nan=False)` plus
    a trailing newline — `allow_nan=False` guarantees the output is strict
    JSON (no bare NaN/Infinity literals).
    """
    if "tolerance" not in payload:
        raise ValueError(
            f"write_fixture({family}/{filename}): payload is missing required 'tolerance' key"
        )

    doc = {
        "family": family,
        "generator": generator,
        "tool_versions": versions,
        **payload,
    }

    out_dir = FIXTURES / family
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / filename
    text = json.dumps(encode(doc), indent=2, allow_nan=False) + "\n"
    out_path.write_text(text)
    return out_path


def write_csv(
    family: str,
    filename: str,
    header: Iterable[str],
    rows: Iterable[Iterable[Any]],
) -> Path:
    """Write a CSV fixture companion under FIXTURES/family/filename.

    Uses the csv module with lineterminator="\\n"; floats are written via
    `repr()` for exact round-trip; missing cells are written as the empty
    string.
    """
    out_dir = FIXTURES / family
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / filename

    def cell(v: Any) -> str:
        if v is None:
            return ""
        if isinstance(v, float):
            return repr(v)
        return str(v)

    with out_path.open("w", newline="") as fh:
        writer = csv.writer(fh, lineterminator="\n")
        writer.writerow(list(header))
        for row in rows:
            writer.writerow([cell(v) for v in row])

    return out_path
