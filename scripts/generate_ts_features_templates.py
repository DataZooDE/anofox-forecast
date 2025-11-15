#!/usr/bin/env python3
"""Regenerate the ts_features override templates (JSON and CSV)."""

from __future__ import annotations

import csv
import json
import subprocess
import tempfile
from pathlib import Path


def format_scalar(value):
    if value is None:
        return ""
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float, str)):
        return value
    return json.dumps(value, ensure_ascii=False)


def main() -> None:
    repo_root = Path(__file__).resolve().parents[1]
    extension_path = repo_root / "build" / "release" / "extension" / "anofox_forecast" / "anofox_forecast.duckdb_extension"
    if not extension_path.exists():
        raise SystemExit(f"Extension not found at {extension_path}")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp_json = Path(tmpdir) / "raw_template.json"
        sql = f"""
LOAD '{extension_path.as_posix()}';
COPY (
    SELECT feature, params_json
    FROM ts_features_config_template()
    ORDER BY feature
) TO '{tmp_json.as_posix()}' (FORMAT JSON, ARRAY true);
"""
        result = subprocess.run(
            ["duckdb", "-unsigned", "-c", sql],
            cwd=repo_root,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            raise SystemExit(f"DuckDB command failed:\n{result.stderr}")

        rows = json.loads(tmp_json.read_text(encoding="utf-8"))

    template = []
    for row in rows:
        feature = row["feature"]
        params_json = row["params_json"] or "{}"
        params = json.loads(params_json)
        template.append({"feature": feature, "params": params})

    data_dir = repo_root / "benchmark" / "timeseries_features" / "data"
    data_dir.mkdir(parents=True, exist_ok=True)

    json_path = data_dir / "all_features_overrides.json"
    with json_path.open("w", encoding="utf-8") as jf:
        json.dump(template, jf, indent=2, ensure_ascii=False)
        jf.write("\n")

    reserved = {"feature"}
    all_keys = sorted({key for row in template for key in row["params"].keys()})
    display_keys = []
    display_to_actual = {}
    for key in all_keys:
        display = f"param_{key}" if key in reserved else key
        display_to_actual[display] = key
        display_keys.append(display)
    csv_path = data_dir / "all_features_overrides.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as cf:
        writer = csv.writer(cf)
        writer.writerow(["feature", *display_keys])
        for row in template:
            params = row["params"]
            values = []
            for display in display_keys:
                actual_key = display_to_actual[display]
                values.append(format_scalar(params.get(actual_key)))
            writer.writerow([row["feature"], *values])

    print(f"Wrote {json_path} and {csv_path}")


if __name__ == "__main__":
    main()

