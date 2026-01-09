-- =============================================================================
-- Demonstrate ts_cv_split() for Backtest Folds
-- =============================================================================
-- This example shows how to create train/test splits for time series
-- cross-validation using the backtest sample dataset.
--
-- Prerequisites: Run 01_sample_data.sql first to create the dataset
-- =============================================================================

-- Load the extension
LOAD anofox_forecast;

-- =============================================================================
-- Step 1: Auto-generate fold boundaries using ts_cv_generate_folds
-- =============================================================================
-- Parameters:
--   source: table name
--   date_col: date column
--   n_folds: number of CV folds to generate
--   horizon: forecast horizon (periods)
--   frequency: data frequency ('1mo' = monthly)
--   initial_train_size: optional, defaults to 50% of data

-- Generate 3 folds with 3-month forecast horizon
SELECT * FROM ts_cv_generate_folds(
    'backtest_sample',   -- source table
    date,                -- date column
    3,                   -- n_folds
    3,                   -- horizon (3 months)
    '1mo'                -- monthly frequency
);

-- Store the training end times for reuse
CREATE OR REPLACE TABLE cv_fold_times AS
SELECT training_end_times
FROM ts_cv_generate_folds('backtest_sample', date, 3, 3, '1mo');

-- =============================================================================
-- Step 2: Examine fold boundaries using ts_cv_split_folds
-- =============================================================================
-- This shows the train/test date ranges for each fold

SELECT *
FROM ts_cv_split_folds(
    'backtest_sample',
    category,
    date,
    (SELECT training_end_times FROM cv_fold_times),
    3,      -- horizon
    '1mo'   -- frequency
);

-- =============================================================================
-- Step 3: Create train/test splits using ts_cv_split
-- =============================================================================
-- Default is 'expanding' window (train grows with each fold)

-- View split summary per fold
SELECT
    fold_id,
    split,
    COUNT(*) AS n_rows,
    MIN(date_col) AS start_date,
    MAX(date_col) AS end_date
FROM ts_cv_split(
    'backtest_sample',
    category,
    date,
    sales,
    (SELECT training_end_times FROM cv_fold_times),
    3,
    '1mo'
)
GROUP BY fold_id, split
ORDER BY fold_id, split DESC;

-- =============================================================================
-- Step 4: Compare window types
-- =============================================================================
-- Expanding window: train set grows with each fold (default)
-- Fixed window: train set stays same size, slides forward

-- Expanding window train sizes (per fold, per category)
SELECT
    'expanding' AS window_type,
    fold_id,
    group_col AS category,
    COUNT(*) FILTER (WHERE split = 'train') AS train_size,
    COUNT(*) FILTER (WHERE split = 'test') AS test_size
FROM ts_cv_split(
    'backtest_sample',
    category,
    date,
    sales,
    (SELECT training_end_times FROM cv_fold_times),
    3,
    '1mo',
    window_type := 'expanding'
)
GROUP BY fold_id, group_col
ORDER BY fold_id, group_col;

-- Fixed window train sizes (12 months minimum training)
SELECT
    'fixed' AS window_type,
    fold_id,
    group_col AS category,
    COUNT(*) FILTER (WHERE split = 'train') AS train_size,
    COUNT(*) FILTER (WHERE split = 'test') AS test_size
FROM ts_cv_split(
    'backtest_sample',
    category,
    date,
    sales,
    (SELECT training_end_times FROM cv_fold_times),
    3,
    '1mo',
    window_type := 'fixed',
    min_train_size := 12
)
GROUP BY fold_id, group_col
ORDER BY fold_id, group_col;

-- =============================================================================
-- Step 5: Store CV splits for later use in forecasting
-- =============================================================================
-- Create a table with all splits for the pipeline

CREATE OR REPLACE TABLE cv_splits AS
SELECT
    group_col AS category,
    date_col AS date,
    target_col AS sales,
    fold_id,
    split
FROM ts_cv_split(
    'backtest_sample',
    category,
    date,
    sales,
    (SELECT training_end_times FROM cv_fold_times),
    3,
    '1mo'
);

-- Verify the splits
SELECT
    category,
    fold_id,
    split,
    COUNT(*) AS n_rows,
    ROUND(AVG(sales), 2) AS avg_sales
FROM cv_splits
GROUP BY category, fold_id, split
ORDER BY category, fold_id, split DESC;

-- =============================================================================
-- Step 6: Join back original features for forecasting
-- =============================================================================
-- The cv_splits table only has the target column - join back features

CREATE OR REPLACE TABLE cv_data_with_features AS
SELECT
    cv.category,
    cv.date,
    cv.sales,
    cv.fold_id,
    cv.split,
    -- Known features (available at forecast time)
    bs.promotion,
    EXTRACT(MONTH FROM cv.date)::INT AS month,
    -- Unknown features (not available at forecast time)
    bs.temperature
FROM cv_splits cv
JOIN backtest_sample bs
    ON cv.category = bs.category
    AND cv.date = bs.date;

-- Preview the data with features
SELECT * FROM cv_data_with_features
WHERE fold_id = 1
ORDER BY category, date
LIMIT 20;

-- =============================================================================
-- Summary Statistics
-- =============================================================================
SELECT
    'Total rows in CV dataset' AS metric,
    COUNT(*)::VARCHAR AS value
FROM cv_data_with_features
UNION ALL
SELECT
    'Number of folds',
    COUNT(DISTINCT fold_id)::VARCHAR
FROM cv_data_with_features
UNION ALL
SELECT
    'Categories',
    STRING_AGG(DISTINCT category, ', ')
FROM cv_data_with_features;
