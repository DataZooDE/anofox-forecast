# DuckDB v1.5.6 vs v2.0.0 (#296)

What DuckDB 2.0 changes for typical anofox_forecast workloads, with the extension code
held fixed: both CLIs are builds of the same extension commit (`feat/duckdb2-adoption`,
2026-10-29), one against DuckDB v1.5.6 and one against v2.0.0.

```sh
python3 benchmark/duckdb2/compare.py \
    --a ../anofox-forecast-v156/build/release/duckdb --b build/release/duckdb --reps 5
```

- Each workload is one query over a Parquet-backed view (the 2.0 async Parquet reader is
  in play), wrapped as `SELECT count(*), <checksum> FROM (<query>)`.
- 5 repetitions per build, **interleaved** (A, B, B, A, ...), each run started only when no
  compiler is running and the 1-minute load is below 4. On this shared machine,
  block-sequential runs once produced fake +40..+320% "regressions".
- Data: synthetic daily series over 365 days (weekly season + trend + noise, ~3% NULLs,
  ~2% gaps): 10k, 2k, 200 or 50 series, depending on the workload's cost.
- Machine: 13th Gen Intel Core i9-13900H (20 threads), 62 GiB RAM, Linux 6.18.

## Results (median of 5)

| Workload | Series | v1.5.6 (s) | v2.0.0 (s) | Δ | Same result |
|---|---:|---:|---:|---:|:---:|
| `ts_fill_gaps_by` | 10k | 0.551 | 0.555 | +0.7% | yes |
| `ts_diff_by` | 10k | 0.361 | 0.363 | +0.6% | yes |
| `ts_stats_by`, VARCHAR ids | 10k | 1.412 | 1.297 | −8.1% | yes |
| `ts_stats_by`, BIGINT ids | 10k | 1.308 | 1.191 | −8.9% | yes |
| `ts_stats_agg` GROUP BY | 10k | 0.146 | 0.117 | −19.9% | yes |
| `ts_features_by` | 2k | 0.988 | 0.972 | −1.6% | yes |
| `ts_forecast_by` Naive | 10k | 0.401 | 0.274 | −31.7% | yes |
| `ts_forecast_by` ETS | 200 | 4.309 | 4.109 | −4.6% | yes |
| `ts_forecast_by` AutoARIMA | 50 | 42.156 | 43.882 | +4.1% | yes |
| `ts_forecast_agg` Naive, `MAP{}` | 10k | 0.245 | 0.228 | −6.9% | yes |
| `ts_forecast_agg` Naive, params set | 10k | 0.294 | 0.266 | −9.5% | yes |
| `ts_aggregate_hierarchy` (3 levels) | 10k | 8.304 | 8.409 | +1.3% | yes |
| same series via `GROUP BY ds, ROLLUP(...)` | 10k | 0.145 | 0.153 | +5.5% | yes |
| `ts_stats_agg` GROUPING SETS | 200 | 0.017 | 0.021 | (+4 ms) | yes |
| `ts_cv_folds_by` + `ts_cv_forecast_by` Naive | 2k | 1.957 | 1.903 | −2.8% | yes |

"Same result": the count and checksum are identical across the two builds for every rep.

## Reading

- **Where the engine is the work, 2.0 is faster:** grouping and list building in
  `ts_stats_by` (−8%), `ts_stats_agg` (−20%) and the cheap forecasts (`Naive` −32%)
  gain from the single-key hash partitioning and the list-based ordered aggregates.
- **Where the model fit is the work, nothing changes:** ETS, AutoARIMA, features and CV
  are within ±5%, the run-to-run spread of these workloads.
- **`ts_aggregate_hierarchy` is ~55× slower than the equivalent `GROUP BY ds, ROLLUP(...)`**
  (identical rows, same checksum) on both lines. That's an extension cost, not an
  engine one (#305). Until then, the ROLLUP recipe in
  `docs/api/02-hierarchical.md` is the fast path.
- Parsing `params` once per group in `ts_forecast_agg` costs ~4 µs per group (0.266 s vs
  0.228 s over 10k groups). That's measurable only on the cheapest model and not worth a
  2.0-only bind-time path.
- `ts_stats` on very long pooled series is O(n²) through approximate entropy (#304). That
  is why the grouping-sets workload has no grand-total level.

## Correctness on 2.0

DuckDB 2.0 runs `UNION ALL` branches in parallel when `preserve_insertion_order = false`
(duckdb/duckdb#24117), the source shape behind #285. `test/sql/correctness/union_source.test`
now runs every check under both settings; all in-out macros stay union-safe.
