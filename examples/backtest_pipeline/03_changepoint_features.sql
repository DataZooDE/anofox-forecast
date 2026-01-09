-- =============================================================================
-- Add Changepoint Features Using BOCPD Algorithm
-- =============================================================================
-- This example shows how to detect changepoints in time series data and
-- use the results as features for forecasting.
--
-- BOCPD = Bayesian Online Changepoint Detection
-- Key parameter: hazard_lambda (expected run length before changepoint)
--   - Lower values = more sensitive to changes
--   - Higher values = fewer false positives
--
-- Prerequisites: Run 01_sample_data.sql first
-- =============================================================================

LOAD anofox_forecast;

-- =============================================================================
-- Step 1: Detect Changepoints Per Category
-- =============================================================================
-- Use ts_detect_changepoints_by for grouped data

SELECT
    id AS category,
    (changepoints).changepoint_indices AS cp_indices,
    length((changepoints).is_changepoint) AS series_length
FROM ts_detect_changepoints_by(
    'backtest_sample',
    category,
    date,
    sales,
    MAP(['hazard_lambda', 'include_probabilities'], ['100.0', 'true'])
);

-- =============================================================================
-- Step 2: Get Changepoint Probabilities for Each Observation
-- =============================================================================
-- This creates a feature that can be used in forecasting models

CREATE OR REPLACE TABLE sales_with_cp AS
WITH cp_data AS (
    SELECT
        id AS category,
        (changepoints).is_changepoint AS is_cp_array,
        (changepoints).changepoint_probability AS cp_prob_array
    FROM ts_detect_changepoints_by(
        'backtest_sample',
        category,
        date,
        sales,
        MAP(['hazard_lambda', 'include_probabilities'], ['100.0', 'true'])
    )
),
-- Number the rows to join back
numbered_sales AS (
    SELECT
        category,
        date,
        sales,
        promotion,
        temperature,
        ROW_NUMBER() OVER (PARTITION BY category ORDER BY date) AS row_num
    FROM backtest_sample
)
SELECT
    ns.category,
    ns.date,
    ns.sales,
    ns.promotion,
    ns.temperature,
    -- Extract changepoint probability for this row
    cp.cp_prob_array[ns.row_num] AS changepoint_probability,
    cp.is_cp_array[ns.row_num] AS is_changepoint
FROM numbered_sales ns
JOIN cp_data cp ON ns.category = cp.category;

-- View the results
SELECT * FROM sales_with_cp
ORDER BY category, date
LIMIT 20;

-- =============================================================================
-- Step 3: Aggregate Changepoint Features Per Series
-- =============================================================================
-- Create summary features that can be used for model selection or weighting

SELECT
    category,
    -- Number of detected changepoints
    SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) AS n_changepoints,
    -- Average changepoint probability (stability measure)
    ROUND(AVG(changepoint_probability), 4) AS avg_cp_probability,
    -- Max changepoint probability (most likely changepoint strength)
    ROUND(MAX(changepoint_probability), 4) AS max_cp_probability,
    -- Count of high-probability points (potential regime changes)
    SUM(CASE WHEN changepoint_probability > 0.1 THEN 1 ELSE 0 END) AS n_high_prob_points
FROM sales_with_cp
GROUP BY category;

-- =============================================================================
-- Step 4: Create Rolling Changepoint Features
-- =============================================================================
-- Use a rolling window to create a "regime change indicator" feature

CREATE OR REPLACE TABLE sales_with_cp_features AS
WITH cp_groups AS (
    SELECT
        category,
        date,
        sales,
        promotion,
        temperature,
        changepoint_probability,
        is_changepoint,
        -- Rolling max of changepoint probability (last 3 months)
        MAX(changepoint_probability) OVER (
            PARTITION BY category
            ORDER BY date
            ROWS BETWEEN 2 PRECEDING AND CURRENT ROW
        ) AS rolling_max_cp_prob,
        -- Cumulative count of changepoints (defines "regime" groups)
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) OVER (
            PARTITION BY category
            ORDER BY date
            ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
        ) AS cumulative_changepoints
    FROM sales_with_cp
)
SELECT
    category,
    date,
    sales,
    promotion,
    temperature,
    changepoint_probability,
    is_changepoint,
    rolling_max_cp_prob,
    cumulative_changepoints,
    -- Distance from last changepoint (in periods)
    ROW_NUMBER() OVER (
        PARTITION BY category, cumulative_changepoints
        ORDER BY date
    ) AS periods_since_changepoint
FROM cp_groups;

-- View the features
SELECT
    category,
    date,
    sales,
    ROUND(changepoint_probability, 4) AS cp_prob,
    is_changepoint,
    ROUND(rolling_max_cp_prob, 4) AS rolling_cp,
    cumulative_changepoints,
    periods_since_changepoint
FROM sales_with_cp_features
ORDER BY category, date;

-- =============================================================================
-- Step 5: Combine with CV Splits for Backtesting
-- =============================================================================
-- Join changepoint features with the cross-validation splits

-- First ensure cv_splits table exists (from previous demo)
CREATE OR REPLACE TABLE cv_fold_times AS
SELECT training_end_times
FROM ts_cv_generate_folds('backtest_sample', date, 3, 3, '1mo');

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

-- Join with changepoint features
CREATE OR REPLACE TABLE cv_data_with_changepoints AS
SELECT
    cv.category,
    cv.date,
    cv.sales,
    cv.fold_id,
    cv.split,
    cf.promotion,
    cf.temperature,
    cf.changepoint_probability,
    cf.rolling_max_cp_prob,
    cf.cumulative_changepoints,
    cf.periods_since_changepoint
FROM cv_splits cv
JOIN sales_with_cp_features cf
    ON cv.category = cf.category
    AND cv.date = cf.date;

-- Summary of changepoint features by fold and split
SELECT
    fold_id,
    split,
    COUNT(*) AS n_rows,
    ROUND(AVG(changepoint_probability), 4) AS avg_cp_prob,
    ROUND(MAX(changepoint_probability), 4) AS max_cp_prob,
    SUM(CASE WHEN changepoint_probability > 0.1 THEN 1 ELSE 0 END) AS high_cp_count
FROM cv_data_with_changepoints
GROUP BY fold_id, split
ORDER BY fold_id, split DESC;

-- =============================================================================
-- Step 6: Alternative - Use Scalar Function for Single Series
-- =============================================================================
-- For single series analysis, use _ts_detect_changepoints_bocpd directly

SELECT
    category,
    -- Get changepoint info as a struct
    _ts_detect_changepoints_bocpd(
        LIST(sales ORDER BY date),
        100.0,  -- hazard_lambda
        true    -- include_probabilities
    ) AS cp_result
FROM backtest_sample
GROUP BY category;
