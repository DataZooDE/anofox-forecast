-- ============================================================================
-- Ensemble Member Introspection — Phase 6 (INSP-01) DoD Tracer
-- ============================================================================
-- Verifies ts_ensemble_inspect_by and ts_auto_ensemble_inspect_by:
--
--   Section 1: Explicit-member Mean weights == 1/k within 1e-10, sum == 1
--   Section 2: AutoEnsemble WeightedMSE (default) weight IS NULL, score > 0,
--              rank IN (1, 2, ..., top_k)
--
-- Run: ./build/release/duckdb -unsigned -batch -c \
--        "LOAD '$PWD/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; \
--         .read examples/forecasting/ensemble_inspect_tracer.sql"
--
-- Precondition: extension built and loaded (make; then LOAD command above).
-- ============================================================================

.print '============================================================================='
.print 'ENSEMBLE INTROSPECTION TRACER — Phase 6 (INSP-01)'
.print '============================================================================='

-- ============================================================================
-- Synthetic data: 2 series with 60 daily observations
-- ============================================================================
CREATE OR REPLACE TABLE insp_series AS
SELECT
    CASE WHEN g = 0 THEN 'series_A' ELSE 'series_B' END AS id,
    (TIMESTAMP '2023-01-01' + INTERVAL (r) DAY)::DATE AS ds,
    10.0 + r * 0.4 + sin(r * 0.2) * 3.0 + g * 5.0 AS y
FROM range(60) t(r), range(2) s(g);

SELECT count(*) AS total_rows FROM insp_series;

-- ============================================================================
-- Section 1: ts_ensemble_inspect_by with combination_method := 'mean'
--
-- DoD assertions:
--   (a) abs(weight - 1/3) <= 1e-10 for ALL member rows (Mean → equal 1/k)
--   (b) sum(weight) per series = 1.0 within 1e-10
-- ============================================================================

.print ''
.print '>>> Section 1: ts_ensemble_inspect_by — Mean combination (INSP-01 DoD)'
.print '--------------------------------------------------------------------'

CREATE OR REPLACE TABLE explicit_mean_inspect AS
SELECT * FROM ts_ensemble_inspect_by(
    'insp_series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Naive'],
    combination_method := 'mean',
    seasonal_period := 0
);

.print ''
.print 'Explicit-member Mean inspection result:'
SELECT * FROM explicit_mean_inspect ORDER BY id, member_name;

.print ''
.print 'DoD assertion (a): count of rows where |weight - 1/3| > 1e-10 (must be 0):'
SELECT
    count(*) FILTER (WHERE abs(weight - 1.0/3.0) > 1e-10) AS unequal_mean_weights
FROM explicit_mean_inspect;

.print ''
.print 'DoD assertion (b): count of groups where |sum(weight) - 1.0| > 1e-10 (must be 0):'
SELECT
    count(*) FILTER (WHERE abs(sum_w - 1.0) > 1e-10) AS bad_weight_sum
FROM (
    SELECT id, SUM(weight) AS sum_w
    FROM explicit_mean_inspect
    GROUP BY id
);

-- ============================================================================
-- Section 2: ts_auto_ensemble_inspect_by — default WeightedMSE (INSP-01 DoD)
--
-- DoD assertions:
--   (a) weight IS NULL for ALL rows (WeightedMSE = crate 0.15.3 limitation)
--   (b) score > 0 for ALL rows (in-sample MSE from all_scores())
--   (c) rank IN (1, 2, 3) for top_k=3
-- ============================================================================

.print ''
.print '>>> Section 2: ts_auto_ensemble_inspect_by — WeightedMSE default (INSP-01 DoD)'
.print '--------------------------------------------------------------------'

-- NOTE: combination_method := 'weighted_mse' explicitly (empty string '' maps to 'mean'
--       via parse_combination_method — pass the explicit string for WeightedMSE behavior)
CREATE OR REPLACE TABLE auto_weighted_mse_inspect AS
SELECT * FROM ts_auto_ensemble_inspect_by(
    'insp_series', id, ds, y,
    top_k := 3,
    combination_method := 'weighted_mse',
    seasonal_period := 0
);

.print ''
.print 'AutoEnsemble WeightedMSE inspection result:'
SELECT * FROM auto_weighted_mse_inspect ORDER BY id, rank;

.print ''
.print 'DoD assertion (a): count of rows where weight IS NOT NULL (must be 0 for WeightedMSE):'
SELECT
    count(*) FILTER (WHERE weight IS NOT NULL) AS non_null_weights
FROM auto_weighted_mse_inspect;

.print ''
.print 'DoD assertion (b): count of rows where score <= 0 or score IS NULL (must be 0):'
SELECT
    count(*) FILTER (WHERE score <= 0 OR score IS NULL) AS bad_scores
FROM auto_weighted_mse_inspect;

.print ''
.print 'DoD assertion (c): count of rows where rank NOT IN (1,2,3) (must be 0 for top_k=3):'
SELECT
    count(*) FILTER (WHERE rank NOT IN (1, 2, 3)) AS bad_ranks
FROM auto_weighted_mse_inspect;

-- ============================================================================
-- Bonus: AutoEnsemble Mean — weight should be Some(1/k)
-- ============================================================================

.print ''
.print '>>> Bonus: ts_auto_ensemble_inspect_by — Mean combination (weight = 1/k)'
.print '--------------------------------------------------------------------'

CREATE OR REPLACE TABLE auto_mean_inspect AS
SELECT * FROM ts_auto_ensemble_inspect_by(
    'insp_series', id, ds, y,
    top_k := 3,
    combination_method := 'mean',
    seasonal_period := 0
);

.print ''
.print 'AutoEnsemble Mean inspection result:'
SELECT * FROM auto_mean_inspect ORDER BY id, rank;

.print ''
.print 'Bonus assertion: weight IS NULL count (must be 0 for Mean):'
SELECT count(*) FILTER (WHERE weight IS NULL) AS null_weights FROM auto_mean_inspect;

.print ''
.print '============================================================================='
.print 'INSP-01 DoD TRACER COMPLETE — check all assertion counts above are 0'
.print '============================================================================='
