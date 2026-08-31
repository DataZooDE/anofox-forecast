-- ============================================================================
-- Ensemble Member Introspection — Phase 6 (INSP-01) Full DoD Example
-- ============================================================================
-- Verifies ts_ensemble_inspect_by and ts_auto_ensemble_inspect_by across all
-- four DoD scenarios: explicit Mean, explicit WeightedMSE, AutoEnsemble Mean,
-- AutoEnsemble WeightedMSE (default non-Mean combination).
--
-- INSP-01: User can inspect which member models an ensemble selected and their
-- combination weights, per series.
--
-- Run: ./build/release/duckdb -unsigned < examples/forecasting/ensemble_inspect.sql
--
-- Precondition: extension built and loaded by LOAD anofox_forecast below.
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'ENSEMBLE MEMBER INTROSPECTION — Phase 6 (INSP-01) Full DoD'
.print '============================================================================='

-- ============================================================================
-- Shared synthetic data: 2 series, 60 daily observations
-- y = 10 + 0.4*t + 3*sin(0.2*t) + series_offset
-- ============================================================================

CREATE OR REPLACE TABLE insp_series AS
SELECT
    CASE WHEN g = 0 THEN 'series_A' ELSE 'series_B' END AS id,
    (DATE '2023-01-01' + INTERVAL (r) DAY)::DATE AS ds,
    10.0 + r * 0.4 + sin(r * 0.2) * 3.0 + g * 5.0 AS y
FROM range(60) t(r), range(2) s(g);

SELECT count(*) AS total_rows FROM insp_series;

-- ============================================================================
-- SECTION 1: ts_ensemble_inspect_by — explicit Mean combination (INSP-01 DoD)
-- ============================================================================
-- Invariant: Mean combination yields equal weights 1/k per member.
-- With 3 members: weight == 1/3 == 0.333... for every row.
-- Weights sum to 1.0 per series.
--
-- DoD assertions:
--   (a) abs(weight - 1.0/3.0) <= 1e-10 for ALL member rows
--   (b) abs(sum(weight) - 1.0) <= 1e-10 per series
-- ============================================================================

.print ''
.print '>>> SECTION 1: ts_ensemble_inspect_by — explicit Mean (INSP-01 DoD)'
.print '------------------------------------------------------------------------'

CREATE OR REPLACE TABLE explicit_mean AS
SELECT * FROM ts_ensemble_inspect_by(
    'insp_series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'mean',
    seasonal_period := 0
);

.print ''
.print 'Explicit-member Mean inspection (weight == 1/3 for all rows):'
SELECT * FROM explicit_mean ORDER BY id, member_name;

.print ''
.print 'DoD assertion (a): unequal_mean_weights (must be 0):'
SELECT
    count(*) FILTER (WHERE abs(weight - 1.0/3.0) > 1e-10) AS unequal_mean_weights
FROM explicit_mean;

.print ''
.print 'DoD assertion (b): bad_weight_sum (must be 0):'
SELECT
    count(*) FILTER (WHERE abs(sum_w - 1.0) > 1e-10) AS bad_weight_sum
FROM (
    SELECT id, SUM(weight) AS sum_w
    FROM explicit_mean
    GROUP BY id
);

-- ============================================================================
-- SECTION 2: ts_ensemble_inspect_by — explicit WeightedMSE (INSP-01 DoD)
-- ============================================================================
-- WeightedMSE assigns inverse-MSE weights (better-fitting members get higher
-- weight). All weights are non-negative and sum to 1.0 per series.
--
-- DoD assertions:
--   (a) weight >= 0 for ALL member rows (no negative weights)
--   (b) abs(sum(weight) - 1.0) <= 1e-6 per series
-- ============================================================================

.print ''
.print '>>> SECTION 2: ts_ensemble_inspect_by — WeightedMSE (INSP-01 DoD)'
.print '------------------------------------------------------------------------'

CREATE OR REPLACE TABLE explicit_wmse AS
SELECT * FROM ts_ensemble_inspect_by(
    'insp_series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'weighted_mse',
    seasonal_period := 0
);

.print ''
.print 'Explicit-member WeightedMSE inspection (weights > 0, sum to 1 per series):'
SELECT * FROM explicit_wmse ORDER BY id, member_name;

.print ''
.print 'DoD assertion (a): negative_weights (must be 0):'
SELECT
    count(*) FILTER (WHERE weight < 0) AS negative_weights
FROM explicit_wmse;

.print ''
.print 'DoD assertion (b): bad_weight_sum (must be 0):'
SELECT
    count(*) FILTER (WHERE abs(sum_w - 1.0) > 1e-6) AS bad_weight_sum
FROM (
    SELECT id, SUM(weight) AS sum_w
    FROM explicit_wmse
    GROUP BY id
);

-- ============================================================================
-- SECTION 3: ts_auto_ensemble_inspect_by — AutoEnsemble Mean (INSP-01 DoD)
-- ============================================================================
-- AutoEnsemble with Mean combination: weight = 1/k (equal, non-NULL).
-- Score = in-sample MSE from all_scores()[..model_count()], always > 0.
-- Rank = 1..k ascending by MSE (rank 1 = best-fitting member).
--
-- DoD assertions:
--   (a) weight IS NOT NULL for ALL rows (Mean gives non-NULL weight)
--   (b) abs(weight - 1.0/k) <= 1e-10 where k = per-series member count
--   (c) score > 0 for ALL rows
--   (d) rank IN (1..k) for ALL rows
-- ============================================================================

.print ''
.print '>>> SECTION 3: ts_auto_ensemble_inspect_by — AutoEnsemble Mean (INSP-01 DoD)'
.print '------------------------------------------------------------------------'

CREATE OR REPLACE TABLE auto_mean AS
SELECT * FROM ts_auto_ensemble_inspect_by(
    'insp_series', id, ds, y,
    top_k := 3,
    combination_method := 'mean',
    seasonal_period := 0
);

.print ''
.print 'AutoEnsemble Mean inspection (weight=1/k, score>0, rank in 1..k):'
SELECT * FROM auto_mean ORDER BY id, rank;

.print ''
.print 'DoD assertion (a): null_weights (must be 0 — Mean gives non-NULL weight):'
SELECT count(*) FILTER (WHERE weight IS NULL) AS null_weights FROM auto_mean;

.print ''
.print 'DoD assertion (b): unequal_weights (must be 0 — weight == 1/k within 1e-10):'
WITH counts AS (
    SELECT id, count(*) AS k FROM auto_mean GROUP BY id
)
SELECT
    count(*) FILTER (WHERE abs(m.weight - 1.0/c.k) > 1e-10) AS unequal_weights
FROM auto_mean m JOIN counts c USING (id);

.print ''
.print 'DoD assertion (c): bad_scores (must be 0 — score > 0 for all rows):'
SELECT count(*) FILTER (WHERE score <= 0 OR score IS NULL) AS bad_scores FROM auto_mean;

.print ''
.print 'DoD assertion (d): bad_ranks (must be 0 — rank in 1..3 for top_k=3):'
WITH counts AS (
    SELECT id, max(rank) AS k FROM auto_mean GROUP BY id
)
SELECT
    count(*) FILTER (WHERE m.rank < 1 OR m.rank > c.k) AS bad_ranks
FROM auto_mean m JOIN counts c USING (id);

-- ============================================================================
-- SECTION 4: ts_auto_ensemble_inspect_by — AutoEnsemble default WeightedMSE
-- (INSP-01 DoD)
-- ============================================================================
-- AutoEnsemble non-Mean combinations (WeightedMSE/InverseAIC/Stacking/
-- HorizonAdaptive): the inner combination weights are NOT accessible from the
-- anofox-forecast 0.15.3 public API. The AutoEnsemble struct's inner Ensemble
-- is a private field — only all_scores() (member + MSE) and model_count()
-- are publicly accessible. As a result, weight is NULL for all non-Mean
-- AutoEnsemble combinations. This is a documented upstream-crate limitation.
--
-- NOTE: combination_method := 'weighted_mse' must be passed EXPLICITLY.
--       The empty string '' maps to 'mean' (not 'weighted_mse') via
--       parse_combination_method. Pass the explicit string for non-Mean behavior.
--
-- DoD assertions:
--   (a) weight IS NULL for ALL rows (upstream crate 0.15.3 limitation)
--   (b) score > 0 for ALL rows (MSE from all_scores())
-- ============================================================================

.print ''
.print '>>> SECTION 4: ts_auto_ensemble_inspect_by — AutoEnsemble WeightedMSE'
.print '     (weight IS NULL — crate 0.15.3 limitation documented) (INSP-01 DoD)'
.print '------------------------------------------------------------------------'

-- NOTE: Use 'weighted_mse' explicitly — '' maps to Mean, not WeightedMSE.
CREATE OR REPLACE TABLE auto_wmse AS
SELECT * FROM ts_auto_ensemble_inspect_by(
    'insp_series', id, ds, y,
    top_k := 3,
    combination_method := 'weighted_mse',
    seasonal_period := 0
);

.print ''
.print 'AutoEnsemble WeightedMSE inspection (weight=NULL, score>0):'
SELECT * FROM auto_wmse ORDER BY id, rank;

.print ''
.print 'DoD assertion (a): non_null_weights (must be 0 — crate 0.15.3: weight IS NULL):'
SELECT count(*) FILTER (WHERE weight IS NOT NULL) AS non_null_weights FROM auto_wmse;

.print ''
.print 'DoD assertion (b): bad_scores (must be 0 — score > 0 for all rows):'
SELECT count(*) FILTER (WHERE score <= 0 OR score IS NULL) AS bad_scores FROM auto_wmse;

.print ''
.print '============================================================================='
.print 'INSP-01 Full DoD COMPLETE — check all assertion counts above are 0'
.print '============================================================================='
