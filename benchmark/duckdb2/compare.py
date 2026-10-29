#!/usr/bin/env python3
"""DuckDB v1.5.6 vs v2.0 on anofox_forecast workloads (#296).

Runs each workload through two CLIs that both have the extension statically linked
(a v1.5.6 build and a v2.0 build of the same extension commit), interleaved
(A, B, B, A, ...) so a load spike hits both builds alike, and only starts a run when
the machine is idle (no ninja / cc1plus / rustc / cargo process, 1-min load below a
threshold). Block-sequential runs on a shared machine produced fake +40..+320%
"regressions" in an earlier comparison.

Each workload is one SQL query over a Parquet-backed view (so the 2.0 async Parquet
reader is exercised), wrapped as `SELECT count(*), <checksum> FROM (<query>)`. The
checksum must be identical across both builds, which doubles as a cross-version
result check.

Usage:
    python3 benchmark/duckdb2/compare.py --a ../anofox-forecast-v156/build/release/duckdb \\
        --b build/release/duckdb [--reps 5] [--only fill_gaps,stats] [--out results.json]

Writes a JSON with every rep (wall seconds, load average) and prints a Markdown table
(median per build, delta of the medians, checksum match).
"""
import argparse
import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")

# Data: N series x 365 days, weekly seasonality + trend + noise, ~3% NULLs, 2% gaps.
# `sid` is a VARCHAR id ("series_00042"), `nid` the same id as BIGINT.
DATASETS = {
    "s10k": 10_000,
    "s2k": 2_000,
    "s200": 200,
    "s50": 50,
}


def gen_sql(name, n):
    path = os.path.join(DATA, f"{name}.parquet")
    return path, f"""
COPY (
    SELECT 'series_' || lpad(s::VARCHAR, 5, '0') AS sid, s AS nid,
           DATE '2023-01-01' + d::INT AS ds,
           CASE WHEN hash(s, d) % 33 = 0 THEN NULL
                ELSE 100 + (s % 50) + 0.05 * d + 10 * sin(2 * pi() * d / 7) + (hash(s, d) % 1000) / 100.0
           END::DOUBLE AS y
    FROM range({n}) a(s), range(365) b(d)
    WHERE hash(s, d, 7) % 50 <> 0
) TO '{path}' (FORMAT parquet, ROW_GROUP_SIZE 122880);
"""


# name -> (dataset, query, checksum expression over the query's columns)
WORKLOADS = {
    # LIST(... ORDER BY date) heavy, cheap compute: isolates the engine gain
    "fill_gaps": ("s10k", "SELECT * FROM ts_fill_gaps_by('src', sid, ds, y, '1d')", "sum(y)"),
    "diff": ("s10k", "SELECT * FROM ts_diff_by('src', sid, ds, y, 1)", "sum(diff_value)"),
    "stats_varchar_id": ("s10k", "SELECT * FROM ts_stats_by('src', sid, ds, y, '1d')", "sum(mean)"),
    "stats_bigint_id": ("s10k", "SELECT * FROM ts_stats_by('src', nid, ds, y, '1d')", "sum(mean)"),
    "stats_agg_groupby": (
        "s10k",
        "SELECT sid, ts_stats_agg(ds::TIMESTAMP, y) AS st FROM src GROUP BY sid",
        "sum((st).mean)",
    ),
    "features": (
        "s2k",
        "CREATE TEMP VIEW src_nonull AS SELECT * FROM src WHERE y IS NOT NULL;\n"
        "SELECT * FROM ts_features_by('src_nonull', sid, ds, y)",
        "sum(abs_energy)",
    ),
    "forecast_naive": ("s10k", "SELECT * FROM ts_forecast_by('src', sid, ds, y, 'Naive', 14, '1d')", "sum(yhat)"),
    "forecast_ets": (
        "s200",
        "SELECT * FROM ts_forecast_by('src', sid, ds, y, 'ETS', 14, '1d', params := {'seasonal_period': 7})",
        "sum(yhat)",
    ),
    "forecast_autoarima": ("s50", "SELECT * FROM ts_forecast_by('src', sid, ds, y, 'AutoARIMA', 14, '1d')", "sum(yhat)"),
    # params parsed once per group in Update: an empty vs a populated params MAP
    "forecast_agg_naive_noparams": (
        "s10k",
        "SELECT sid, ts_forecast_agg(ds::TIMESTAMP, y, 'Naive', 7, MAP{}) AS f FROM src GROUP BY sid",
        "sum(list_sum((f).point_forecast))",
    ),
    "forecast_agg_naive_params": (
        "s10k",
        "SELECT sid, ts_forecast_agg(ds::TIMESTAMP, y, 'Naive', 7, "
        "MAP{'confidence_level': '0.9'}) AS f FROM src GROUP BY sid",
        "sum(list_sum((f).point_forecast))",
    ),
    # hierarchy: built-in function vs GROUP BY ROLLUP (2.0 cascades the coarser levels, #299);
    # both build the same 3-level series, so their checksums match each other too
    "hierarchy_native": (
        "s10k",
        "SELECT * FROM ts_aggregate_hierarchy((SELECT ds, y, 'R' || (nid % 10) AS region, "
        "'S' || (nid % 100) AS store, sid FROM src), MAP{})",
        "sum(y)",
    ),
    "hierarchy_rollup": (
        "s10k",
        "SELECT CASE WHEN GROUPING(region) = 1 THEN 'AGGREGATED' ELSE region END || '|' || "
        "CASE WHEN GROUPING(store) = 1 THEN 'AGGREGATED' ELSE store END || '|' || "
        "CASE WHEN GROUPING(sid) = 1 THEN 'AGGREGATED' ELSE sid END AS unique_id, ds, SUM(y) AS y "
        "FROM (SELECT ds, y, 'R' || (nid % 10) AS region, 'S' || (nid % 100) AS store, sid FROM src) "
        "GROUP BY ds, ROLLUP(region, store, sid)",
        "sum(y)",
    ),
    # per series + per region (10 series x 365 rows) in one grouping-sets pass. No grand
    # total: ts_stats' approximate entropy is O(n^2) in the series length, so a 73k-row
    # pooled "series" would dominate (see the follow-up issue in the results doc).
    "stats_agg_grouping_sets": (
        "s200",
        "SELECT ts_stats_agg(ds::TIMESTAMP, y) AS st FROM (SELECT ds, y, nid % 20 AS region, sid FROM src) "
        "GROUP BY GROUPING SETS ((region, sid), (region))",
        "sum((st).mean)",
    ),
    "cv_naive": (
        "s2k",
        # setup statement (timed too): the folds table the CV forecast reads by name
        "CREATE TEMP TABLE folds AS SELECT * FROM ts_cv_folds_by('src', sid, ds, y, 3, 14, MAP{});\n"
        "SELECT * FROM ts_cv_forecast_by('folds', sid, ds, y, 'Naive', MAP{})",
        "sum(yhat)",
    ),
}


def busy():
    for proc in ("ninja", "cc1plus", "rustc", "cargo", "make"):
        if subprocess.run(["pgrep", "-x", proc], capture_output=True).returncode == 0:
            return proc
    return None


def wait_idle(max_load, max_wait):
    start = time.time()
    while time.time() - start < max_wait:
        b = busy()
        load = os.getloadavg()[0]
        if b is None and load < max_load:
            return load
        time.sleep(15)
    return os.getloadavg()[0]


def run_cli(cli, sql, timeout=3600):
    with tempfile.TemporaryDirectory() as tmp:
        proc = subprocess.run(
            [cli, "-csv", "-noheader"], input=sql, capture_output=True, text=True, cwd=tmp,
            env={**os.environ, "HOME": tmp, "DATAZOO_DISABLE_TELEMETRY": "1"}, timeout=timeout,
        )
    return proc.returncode, proc.stdout, proc.stderr


def workload_sql(name):
    ds, query, checksum = WORKLOADS[name]
    path = os.path.join(DATA, f"{ds}.parquet")
    *setup, query = query.split(";\n")
    setup = "".join(stmt + ";\n" for stmt in setup)
    return f"""
CREATE VIEW src AS SELECT * FROM read_parquet('{path}');
.timer on
{setup}SELECT count(*), round({checksum}, 4) FROM ({query});
"""


TIMER_RE = re.compile(r"Run Time \(s\): real ([0-9.]+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", required=True, help="CLI of build A (e.g. v1.5.6)")
    ap.add_argument("--b", required=True, help="CLI of build B (e.g. v2.0)")
    ap.add_argument("--reps", type=int, default=5)
    ap.add_argument("--only", default="")
    ap.add_argument("--max-load", type=float, default=4.0)
    ap.add_argument("--max-wait", type=int, default=1800)
    ap.add_argument("--out", default=os.path.join(HERE, "results.json"))
    args = ap.parse_args()

    args.a, args.b = os.path.abspath(args.a), os.path.abspath(args.b)
    os.makedirs(DATA, exist_ok=True)
    for name, n in DATASETS.items():
        path, sql = gen_sql(name, n)
        if not os.path.exists(path):
            rc, out, err = run_cli(args.b, sql)
            if rc != 0:
                sys.exit(f"data generation failed: {err}")

    builds = {"A": args.a, "B": args.b}
    versions = {}
    for k, cli in builds.items():
        rc, out, err = run_cli(cli, "SELECT version();")
        versions[k] = out.strip()

    names = [n for n in WORKLOADS if not args.only or n in args.only.split(",")]
    results = {"versions": versions, "reps": args.reps, "workloads": {}}
    for name in names:
        sql = workload_sql(name)
        runs = {"A": [], "B": []}
        checks = {"A": set(), "B": set()}
        for rep in range(args.reps):
            order = ["A", "B"] if rep % 2 == 0 else ["B", "A"]
            for k in order:
                load = wait_idle(args.max_load, args.max_wait)
                rc, out, err = run_cli(builds[k], sql)
                if rc != 0:
                    sys.exit(f"{name} failed on {versions[k]}: {err.strip()[:500]}")
                t = TIMER_RE.findall(out)
                lines = [l for l in out.splitlines() if l and not l.startswith("Run Time")]
                checks[k].add(lines[-1] if lines else "")
                runs[k].append({"seconds": round(sum(float(x) for x in t), 4), "load": round(load, 2)})
        med = {k: statistics.median(r["seconds"] for r in runs[k]) for k in runs}
        results["workloads"][name] = {
            "runs": runs,
            "median": med,
            "delta_pct": round(100.0 * (med["B"] - med["A"]) / med["A"], 1),
            "checksum": {k: sorted(v) for k, v in checks.items()},
            "checksum_match": checks["A"] == checks["B"] and len(checks["A"]) == 1,
        }
        print(f"{name}: A {med['A']:.3f}s  B {med['B']:.3f}s  ({results['workloads'][name]['delta_pct']:+.1f}%)", flush=True)

    with open(args.out, "w") as f:
        json.dump(results, f, indent=2)

    print()
    print(f"| Workload | {versions['A']} (s) | {versions['B']} (s) | Δ | Same result |")
    print("|---|---:|---:|---:|:---:|")
    for name in names:
        w = results["workloads"][name]
        print(f"| `{name}` | {w['median']['A']:.3f} | {w['median']['B']:.3f} | {w['delta_pct']:+.1f}% | "
              f"{'yes' if w['checksum_match'] else 'NO'} |")


if __name__ == "__main__":
    main()
