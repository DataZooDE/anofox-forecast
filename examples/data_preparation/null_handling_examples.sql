-- ============================================================================
-- NULL Handling Examples — Phase 12 (RUST-01/RUST-02)
-- ============================================================================
-- Proves, against the BUILT extension, that:
--   - Paired metrics use pairwise-complete observations computed in Rust
--     (D-02): a pair/row is dropped the moment ANY input is NULL at that
--     position, not spliced independently per array in C++.
--   - Constant/literal list arguments are read correctly on EVERY row
--     (D-01), not just the first row of a chunk.
--
-- Every section ends with a self-checking assertion of the form
--   SELECT CASE WHEN <condition> THEN 'PASS <label>' ELSE error('FAIL ...') END;
-- so a wrong result aborts the whole script under -bail instead of silently
-- printing a bad number.
--
-- Later Phase 12 plans append their own sections before the final sentinel.
--
-- Run: ./build/release/duckdb -unsigned -bail < examples/data_preparation/null_handling_examples.sql
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'NULL HANDLING EXAMPLES — Phase 12 (RUST-01/RUST-02)'
.print '============================================================================='

-- ============================================================================
-- SECTION 1 — Paired metrics: pairwise-complete NULL policy (RUST-02, D-02)
-- ============================================================================

.print ''
.print 'SECTION 1: Paired metrics use pairwise-complete observations'
.print ''

-- ts_mae([1,NULL,3,4],[1,2,NULL,4]): complete pairs are (1,1) and (4,4) -> MAE=0.
-- Before this phase, each array's NULLs were dropped independently in C++
-- (misaligning the pairs to [1,3,4] vs [1,2,4]), giving 0.333.
SELECT CASE WHEN (
    SELECT ts_mae([1, NULL, 3, 4]::DOUBLE[], [1, 2, NULL, 4]::DOUBLE[]) = 0.0
) THEN 'PASS pairwise_complete_mae' ELSE error(
    'FAIL pairwise_complete_mae: got ' ||
    (SELECT ts_mae([1, NULL, 3, 4]::DOUBLE[], [1, 2, NULL, 4]::DOUBLE[]))::VARCHAR
) END AS result;

-- No complete pair remaining -> SQL NULL, not an error (D-04).
SELECT CASE WHEN (
    SELECT ts_mae([NULL, 1]::DOUBLE[], [1, NULL]::DOUBLE[]) IS NULL
) THEN 'PASS no_complete_pair_is_null' ELSE error('FAIL no_complete_pair_is_null') END AS result;

-- ============================================================================
-- SECTION 2 — Constant list arguments correct on every row (RUST-01, D-01)
-- ============================================================================

.print ''
.print 'SECTION 2: Constant/literal list arguments are correct on every row'
.print ''

CREATE OR REPLACE TABLE five AS SELECT i, [i, i + 1, i + 2]::DOUBLE[] AS a FROM range(5) t(i);

-- ts_mae(a, [0,0,0]) broadcasts a constant second argument across every row.
-- Before this phase, only row 0 of each chunk was read correctly; every
-- subsequent row returned NULL/garbage.
SELECT CASE WHEN (
    SELECT count(*) FROM five WHERE ts_mae(a, [0, 0, 0]::DOUBLE[]) = i + 1
) = 5 THEN 'PASS constant_list_correct_every_row' ELSE error(
    'FAIL constant_list_correct_every_row: got ' ||
    (SELECT count(*) FROM five WHERE ts_mae(a, [0, 0, 0]::DOUBLE[]) = i + 1)::VARCHAR || ' of 5'
) END AS result;

-- ============================================================================
-- SECTION 2 — Single-series analyses: edge trim, interior-NULL error (RUST-02, D-03/D-04)
-- ============================================================================

.print ''
.print 'SECTION 2: Single-series analyses trim edges and error on interior NULLs'
.print ''

CREATE OR REPLACE TABLE series_48 AS
SELECT list_transform(range(48), i -> (10 + 3 * sin(2 * pi() * i / 12) + 0.05 * i)::DOUBLE) AS v;

-- Edge trim parity: [NULL] || v || [NULL] gives the same ts_adf result as v
-- itself — leading/trailing NULLs are trimmed in Rust before the analysis.
SELECT CASE WHEN (
    SELECT ts_adf(list_concat(list_concat([NULL]::DOUBLE[], v), [NULL]::DOUBLE[])) = ts_adf(v)
    FROM series_48
) THEN 'PASS single_series_edge_trim_parity' ELSE error(
    'FAIL single_series_edge_trim_parity'
) END AS result;

-- An interior NULL is a hard error naming its 1-based position, not a silent
-- splice. try() converts the error to NULL for this self-check; without
-- try(), the statement fails with "ts_adf: Invalid input: interior NULL at
-- position 6".
SELECT CASE WHEN (
    SELECT try(ts_adf(list_concat(list_concat(v[1:5], [NULL]::DOUBLE[]), v[6:48]))) IS NULL
    FROM series_48
) THEN 'PASS single_series_interior_null_errors' ELSE error(
    'FAIL single_series_interior_null_errors'
) END AS result;

-- ============================================================================
-- SECTION 3 — Features aggregate parity and detection edge trimming (D-11, D-03)
-- ============================================================================

.print ''
.print 'SECTION 3: ts_features_agg no longer splices NULLs; ts_detect_changepoints stays row-aligned under leading NULLs'
.print ''

CREATE OR REPLACE TABLE fe_example AS
SELECT 'A' AS id, (DATE '2024-01-01' + i * INTERVAL 1 DAY)::TIMESTAMP AS ds,
       CASE WHEN i IN (0, 29) THEN NULL ELSE (10 + sin(i))::DOUBLE END AS y
FROM range(30) t(i);

CREATE OR REPLACE TABLE fe_example_trim AS SELECT * FROM fe_example WHERE y IS NOT NULL;

-- ts_features_agg on a series with edge NULLs equals both the trimmed series
-- AND the ts_features_by table surface on the same (untrimmed) series (D-11).
SELECT CASE WHEN (
    SELECT (SELECT (ts_features_agg(ds, y)).mean FROM fe_example) IS NOT DISTINCT FROM
           (SELECT (ts_features_agg(ds, y)).mean FROM fe_example_trim)
       AND (SELECT (ts_features_agg(ds, y)).mean FROM fe_example) IS NOT DISTINCT FROM
           (SELECT mean FROM ts_features_by('fe_example', id, ds, y))
) THEN 'PASS features_agg_matches_trimmed_and_features_by' ELSE error(
    'FAIL features_agg_matches_trimmed_and_features_by'
) END AS result;

-- The single-series ts_detect_changepoints macro is json-gated (uses to_json).
LOAD json;

CREATE OR REPLACE TABLE cp_leading_null AS
SELECT i AS ds, CASE WHEN i <= 2 THEN NULL ELSE (10 + 3 * sin(2 * pi() * i / 12) + 0.05 * i) END AS val
FROM generate_series(1, 40) t(i);

-- With 2 leading-NULL rows (i=1,2; i=3 is the first non-NULL -> ds=1,2 are
-- "before" the trimmed series), those two rows get NULL is_changepoint and
-- every row with a value gets a non-NULL flag (the `_lead` shift in the
-- macro keeps the BOCPD output row-aligned under edge trimming, D-03).
SELECT CASE WHEN (
    SELECT count(*) FROM ts_detect_changepoints('cp_leading_null', ds, val, NULL)
    WHERE date_col IN (1, 2) AND is_changepoint IS NULL
) = 2
   AND (
    SELECT count(*) FROM ts_detect_changepoints('cp_leading_null', ds, val, NULL)
    WHERE value_col IS NOT NULL AND is_changepoint IS NOT NULL
) = 38
THEN 'PASS changepoints_macro_row_aligned_under_leading_nulls' ELSE error(
    'FAIL changepoints_macro_row_aligned_under_leading_nulls'
) END AS result;

-- ============================================================================
-- SECTION 4 — Conformal/bootstrap NULL classes and per-step validation (D-02, D-10)
-- ============================================================================

.print ''
.print 'SECTION 4: conformal/bootstrap NULL policy classes and per-step residual-count validation'
.print ''

-- Paired evaluation metrics (coverage, mean_interval_width) drop incomplete
-- pairs via complete_cases in Rust (D-02).
SELECT CASE WHEN (
    SELECT ts_conformal_coverage([1, 2, 3]::DOUBLE[], [0, NULL, 4]::DOUBLE[], [2, 3, 5]::DOUBLE[]) = 0.5
) THEN 'PASS conformal_coverage_drops_incomplete_pairs' ELSE error(
    'FAIL conformal_coverage_drops_incomplete_pairs'
) END AS result;

-- A NULL forecast position is position-preserving: ts_conformal_intervals
-- gives NULL lower/upper at exactly that position, not a spliced-out array.
SELECT CASE WHEN (
    SELECT (ts_conformal_intervals([10, NULL, 12]::DOUBLE[], 2.0)).lower IS NOT DISTINCT FROM [8.0, NULL, 10.0]
       AND (ts_conformal_intervals([10, NULL, 12]::DOUBLE[], 2.0)).upper IS NOT DISTINCT FROM [12.0, NULL, 14.0]
) THEN 'PASS conformal_intervals_null_forecast_position_preserving' ELSE error(
    'FAIL conformal_intervals_null_forecast_position_preserving'
) END AS result;

-- D-10: per-step conformal errors in Rust when the flat residual count is
-- not a multiple of horizon, naming both counts — no silent n_folds
-- truncation. try() turns the error into NULL for this self-checking script.
SELECT CASE WHEN (
    SELECT try(ts_conformal_predict_per_step(
        [1, 2, 3, 4, 5, 6, 7]::DOUBLE[], [1, 2, 3, 4, 5, 6, 7]::DOUBLE[],
        [1, 2, 3]::DOUBLE[], 0.1, 3
    )) IS NULL
) THEN 'PASS per_step_residual_count_not_multiple_of_horizon_errors' ELSE error(
    'FAIL per_step_residual_count_not_multiple_of_horizon_errors'
) END AS result;

-- ============================================================================
-- SECTION 5 — Calendar-correct forecast dates and agg NULL parity (D-11, D-12)
-- ============================================================================

.print ''
.print 'SECTION 5: calendar-correct forecast dates (D-12) and ts_forecast_agg NULL parity with ts_forecast_by (D-11)'
.print ''

CREATE OR REPLACE TABLE calendar_me AS
SELECT 'A' AS id, last_day(DATE '2023-01-01' + INTERVAL (i) MONTH) AS ds, (100 + i)::DOUBLE AS y
FROM range(11) t(i);

-- A series of month-end dates ending 2023-11-30 forecasts month-ends with no
-- drift into a 30-day month (D-12) — both ts_forecast_agg (Rust
-- auto-detected frequency) and ts_forecast_by ('1mo') agree.
SELECT CASE WHEN (
    SELECT (ts_forecast_agg(ds::TIMESTAMP, y, 'Naive', 3, MAP{})).forecast_timestamp =
        [TIMESTAMP '2023-12-31 00:00:00', TIMESTAMP '2024-01-31 00:00:00', TIMESTAMP '2024-02-29 00:00:00']
    FROM calendar_me
) THEN 'PASS calendar_month_end_series_agg_forecasts_month_ends_no_drift' ELSE error(
    'FAIL calendar_month_end_series_agg_forecasts_month_ends_no_drift'
) END AS result;

SELECT CASE WHEN (
    SELECT list(ds ORDER BY forecast_step) = [DATE '2023-12-31', DATE '2024-01-31', DATE '2024-02-29']
    FROM ts_forecast_by('calendar_me', id, ds, y, 'Naive', 3, '1mo', MAP{})
) THEN 'PASS calendar_month_end_series_by_forecasts_month_ends_no_drift' ELSE error(
    'FAIL calendar_month_end_series_by_forecasts_month_ends_no_drift'
) END AS result;

CREATE OR REPLACE TABLE calendar_trailing_null_demo AS
SELECT 'A' AS id, (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
       CASE WHEN i >= 30 THEN NULL ELSE i::DOUBLE END AS y
FROM range(32) AS t(i);

-- D-11: ts_forecast_agg no longer splices NULL-valued trailing rows out
-- before fitting — its forecast origin stays anchored to the series'
-- ORIGINAL last row, exactly like ts_forecast_by, on the same fixture
-- docs/api/07-forecasting.md uses for the input-handling contract.
SELECT CASE WHEN (
    SELECT
        (SELECT (ts_forecast_agg(ds::TIMESTAMP, y, 'RandomWalkDrift', 3, MAP{})).forecast_timestamp[1]
         FROM calendar_trailing_null_demo)
        = (SELECT ds::TIMESTAMP FROM ts_forecast_by('calendar_trailing_null_demo', id, ds, y, 'RandomWalkDrift', 3, '1d', MAP{})
           WHERE forecast_step = 1)
    AND
        (SELECT (ts_forecast_agg(ds::TIMESTAMP, y, 'RandomWalkDrift', 3, MAP{})).point_forecast[1]
         FROM calendar_trailing_null_demo)
        = (SELECT yhat FROM ts_forecast_by('calendar_trailing_null_demo', id, ds, y, 'RandomWalkDrift', 3, '1d', MAP{})
           WHERE forecast_step = 1)
) THEN 'PASS calendar_agg_trailing_null_matches_by_forecast_origin_and_value' ELSE error(
    'FAIL calendar_agg_trailing_null_matches_by_forecast_origin_and_value'
) END AS result;

.print ''
.print '============================================================================='
.print 'ALL SECTIONS PASSED'
.print '============================================================================='
