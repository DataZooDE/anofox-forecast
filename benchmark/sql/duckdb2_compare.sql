-- DuckDB 2.0 vs v1.5.5 SQL micro-benchmark for anofox_forecast.
--
-- Purpose: a frozen, identical-for-both-builds set of timed SQL statements exercising
-- forecasting, stats, features, cross-validation, and gap-filling, used to compare
-- wall-clock time between the DuckDB v2.0-cyanoptera build and the v1.5.5 baseline.
--
-- Run from the MAIN repo root (benchmark/duckdb2_compare.py's sqlbench subcommand does
-- this for you): paths below are relative to that root. The extension itself is NEVER
-- hardcoded here — the caller supplies it via the CLI flag, e.g.:
--   <tree>/build/release/duckdb -unsigned -cmd "LOAD '<ext path>'" < benchmark/sql/duckdb2_compare.sql
-- Telemetry is the caller's responsibility (DATAZOO_DISABLE_TELEMETRY=1 in the subprocess
-- environment); this script does not set it. The driver runs this file 3x per CLI/build
-- and reports the per-label median of each BENCH line's "Run Time (s): real" value.
--
-- Sizing: N=100 (the first 100 lexicographically-first M4 Daily unique_ids) was chosen so
-- every timed statement finishes in roughly 1-60s on the v1.5.5 baseline (features_by and
-- cv_forecast are the slowest, at ~30s and ~50s respectively; the forecast_* single-model
-- calls and stats_by/fill_gaps are sub-second to a few seconds). This file is frozen once
-- measurement starts and is identical for both builds.

-- Untimed setup.
.timer off
SET preserve_insertion_order = false;

CREATE TABLE m4s AS
    SELECT * FROM read_parquet('benchmark/data/m4_daily_train_long.parquet')
    WHERE unique_id IN (
        SELECT DISTINCT unique_id FROM read_parquet('benchmark/data/m4_daily_train_long.parquet')
        ORDER BY unique_id LIMIT 100
    );

-- Deterministic gaps: drop every day-of-month-15 row (same input, same removed rows, every run).
CREATE TABLE m4gap AS SELECT * FROM m4s WHERE day(ds) != 15;

.timer on

.print BENCH forecast_naive
SELECT count(*), round(avg(yhat), 4) FROM ts_forecast_by(m4s, unique_id, ds, y, 'Naive', 14, '1d');

.print BENCH forecast_autoets
SELECT count(*), round(avg(yhat), 4) FROM ts_forecast_by(m4s, unique_id, ds, y, 'AutoETS', 14, '1d');

.print BENCH forecast_theta
SELECT count(*), round(avg(yhat), 4) FROM ts_forecast_by(m4s, unique_id, ds, y, 'Theta', 14, '1d');

.print BENCH forecast_mfles
SELECT count(*), round(avg(yhat), 4) FROM ts_forecast_by(m4s, unique_id, ds, y, 'MFLES', 14, '1d');

.print BENCH stats_by
SELECT count(*) FROM ts_stats_by(m4s, unique_id, ds, y, '1d');

.print BENCH features_by
SELECT count(*) FROM ts_features_by(m4s, unique_id, ds, y);

.print BENCH cv_folds
CREATE OR REPLACE TEMP TABLE folds AS SELECT * FROM ts_cv_folds_by(m4s, unique_id, ds, y, 3, 14);

.print BENCH cv_forecast
SELECT count(*), round(avg(yhat), 4) FROM ts_cv_forecast_by(folds, unique_id, ds, y, 'AutoETS');

.print BENCH fill_gaps
SELECT count(*) FROM ts_fill_gaps_by(m4gap, unique_id, ds, y, '1d');
