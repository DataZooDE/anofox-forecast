"""
CLI utility for extracting tsfresh features per `unique_id`.

Usage:
    uv run python benchmark/timeseries_features/tsfresh_features.py \\
        --input_path input.parquet --output_path output.parquet

The script automatically applies feature definitions from
`benchmark/timeseries_features/data/features_overrides.json`.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable

import pandas as pd
from fire import Fire
from tsfresh import extract_features


REQUIRED_COLUMNS = {"unique_id", "ds", "y"}
FEATURES_OVERRIDE_PATH = (
    Path(__file__).resolve().parent / "data" / "features_overrides.json"
)


def _validate_columns(columns: Iterable[str]) -> None:
    missing = REQUIRED_COLUMNS.difference(columns)
    if missing:
        missing_cols = ", ".join(sorted(missing))
        raise ValueError(
            f"Input parquet is missing required columns: {missing_cols}"
        )


def _prepare_dataframe(df: pd.DataFrame) -> pd.DataFrame:
    tsfresh_df = (
        df.rename(columns={"unique_id": "id", "ds": "time", "y": "value"})[
            ["id", "time", "value"]
        ]
        .copy()
    )
    tsfresh_df["time"] = pd.to_datetime(tsfresh_df["time"])
    return tsfresh_df


def _load_feature_overrides(path: Path) -> dict[str, list[dict[str, Any]] | None]:
    if not path.exists():
        raise FileNotFoundError(f"Feature override file not found: {path}")

    with path.open("r", encoding="utf-8") as infile:
        config = json.load(infile)

    overrides: dict[str, list[dict[str, Any]] | None] = {}
    for entry in config:
        feature = entry["feature"]
        params = entry.get("params")
        if params in (None, {}):
            overrides[feature] = None
            continue

        if feature in overrides and overrides[feature] is None:
            raise ValueError(
                f"Conflicting parameter definitions for feature '{feature}'"
            )

        overrides.setdefault(feature, []).append(params)

    return overrides


def compute_tsfresh_features(
    input_path: str | Path,
    output_path: str | Path,
    *,
    n_jobs: int | None = 0,
) -> Path:
    """
    Compute tsfresh comprehensive features grouped by `unique_id`.

    Parameters
    ----------
    input_path:
        Path to the input parquet containing columns `unique_id`, `ds`, `y`.
    output_path:
        Destination parquet path for the feature matrix (one row per unique_id).
    n_jobs:
        Number of parallel workers passed to tsfresh. Defaults to 0 (use all cores).
    """

    input_path = Path(input_path)
    output_path = Path(output_path)

    df = pd.read_parquet(input_path)
    _validate_columns(df.columns)

    tsfresh_df = _prepare_dataframe(df)
    fc_parameters = _load_feature_overrides(FEATURES_OVERRIDE_PATH)

    features = extract_features(
        tsfresh_df,
        column_id="id",
        column_sort="time",
        column_value="value",
        default_fc_parameters=fc_parameters,
        n_jobs=n_jobs,
        disable_progressbar=False,
    )

    features.index.name = "unique_id"
    features = features.reset_index()
    features.to_parquet(output_path, index=False)

    return output_path


def main(
    input_path: str,
    output_path: str,
    n_jobs: int | None = 0,
) -> str:
    """
    Fire entry point wrapping compute_tsfresh_features.
    """

    result_path = compute_tsfresh_features(
        input_path=input_path, output_path=output_path, n_jobs=n_jobs
    )
    return str(result_path)


if __name__ == "__main__":
    Fire(main)

