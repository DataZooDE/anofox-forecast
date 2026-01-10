-- =============================================================================
-- Demonstrate Parallel Fold Execution with ts_cv_forecast_by
-- =============================================================================
-- This example shows how to run forecasts for all CV folds in parallel,
-- leveraging DuckDB's vectorization for massive performance gains.
--
-- Prerequisites: Run 01_sample_data.sql first to create the dataset
-- =============================================================================

-- Load the extension
LOAD anofox_forecast;

-- =============================================================================
-- Step 1: Create CV Splits (from 02_ts_cv_split_demo.sql)
-- =============================================================================

-- Generate 3 folds with 3-month forecast horizon
CREATE OR REPLACE TABLE cv_fold_times AS
SELECT training_end_times
FROM ts_cv_generate_folds('backtest_sample', date, 3, 3, '1mo', MAP{});

-- Create train/test splits
CREATE OR REPLACE TABLE cv_splits AS
SELECT *
FROM ts_cv_split(
    'backtest_sample',
    category,
    date,
    sales,
    (SELECT training_end_times FROM cv_fold_times),
    3,
    '1mo',
    MAP{}
);

-- Verify split structure
SELECT
    fold_id,
    split,
    COUNT(*) AS n_rows,
    MIN(date_col)::DATE AS start_date,
    MAX(date_col)::DATE AS end_date
FROM cv_splits
GROUP BY fold_id, split
ORDER BY fold_id, split DESC;

-- =============================================================================
-- Step 2: Parallel Forecast Execution with ts_cv_forecast_by
-- =============================================================================
-- This runs forecasts for ALL folds and ALL series in ONE query.
-- DuckDB automatically parallelizes across folds and series.

-- Extract training data only
CREATE OR REPLACE TABLE cv_train AS
SELECT * FROM cv_splits WHERE split = 'train';

-- Generate forecasts for all folds in parallel!
-- This processes: 3 folds × 2 categories = 6 models simultaneously
CREATE OR REPLACE TABLE cv_forecasts AS
SELECT *
FROM ts_cv_forecast_by(
    'cv_train',
    group_col,      -- Category column from cv_splits
    date_col,       -- Date column from cv_splits
    target_col,     -- Target column from cv_splits
    'AutoETS',      -- Forecast method
    3,              -- Horizon (3 months)
    MAP{},          -- Model params
    '1mo'           -- Monthly frequency
);

-- View forecast results
SELECT * FROM cv_forecasts
ORDER BY fold_id, id, forecast_step
LIMIT 20;

-- =============================================================================
-- Step 3: Join Forecasts with Actuals for Evaluation
-- =============================================================================

-- Get test actuals
CREATE OR REPLACE TABLE cv_test AS
SELECT
    fold_id,
    group_col AS category,
    date_col AS date,
    target_col AS actual
FROM cv_splits
WHERE split = 'test';

-- Join forecasts with actuals
CREATE OR REPLACE TABLE cv_evaluation AS
SELECT
    f.fold_id,
    f.id AS category,
    f.date,
    f.point_forecast,
    t.actual,
    ABS(f.point_forecast - t.actual) AS abs_error,
    f.model_name
FROM cv_forecasts f
JOIN cv_test t
    ON f.fold_id = t.fold_id
    AND f.id = t.category
    AND f.date = t.date;

SELECT * FROM cv_evaluation ORDER BY fold_id, category, date;

-- =============================================================================
-- Step 4: Calculate Metrics Per Fold
-- =============================================================================

SELECT
    fold_id,
    category,
    COUNT(*) AS n_forecasts,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(abs_error * abs_error)), 2) AS rmse,
    model_name
FROM cv_evaluation
GROUP BY fold_id, category, model_name
ORDER BY fold_id, category;

-- =============================================================================
-- Step 5: Aggregate Across All Folds
-- =============================================================================
-- This gives the overall backtest performance

SELECT
    category,
    COUNT(*) AS total_forecasts,
    ROUND(AVG(abs_error), 2) AS avg_mae,
    ROUND(SQRT(AVG(abs_error * abs_error)), 2) AS avg_rmse,
    ROUND(STDDEV(abs_error), 2) AS std_error
FROM cv_evaluation
GROUP BY category
ORDER BY category;

-- =============================================================================
-- Performance Comparison: Serial vs Parallel
-- =============================================================================
--
-- SERIAL (old approach):
--   FOR fold IN 1..3:
--     FOR category IN ['A', 'B']:
--       SELECT * FROM ts_forecast_by(...) WHERE fold_id = $fold AND category = $cat
--
-- PARALLEL (new approach with ts_cv_forecast_by):
--   SELECT * FROM ts_cv_forecast_by(...)  -- ALL folds + categories at once!
--
-- With DuckDB's thread pool, the parallel version can be 3-10x faster
-- depending on available CPU cores and model complexity.
--
-- =============================================================================

-- =============================================================================
-- Alternative: Manual Compound Key Pattern
-- =============================================================================
-- If you need more control, you can use ts_forecast_by with a compound key:

-- SELECT
--     SPLIT_PART(id, '|', 1) AS category,
--     SPLIT_PART(id, '|', 2)::INT AS fold_id,
--     forecast_step,
--     date,
--     point_forecast
-- FROM ts_forecast_by(
--     'cv_train',
--     group_col || '|' || fold_id::VARCHAR,  -- Compound key
--     date_col,
--     target_col,
--     'AutoETS',
--     3,
--     MAP{}
-- );

-- =============================================================================
-- Cleanup
-- =============================================================================

DROP TABLE cv_fold_times;
DROP TABLE cv_splits;
DROP TABLE cv_train;
DROP TABLE cv_forecasts;
DROP TABLE cv_test;
DROP TABLE cv_evaluation;
