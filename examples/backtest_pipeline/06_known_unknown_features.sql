-- =============================================================================
-- Demonstrate Known vs Unknown Feature Handling with ts_fill_unknown()
-- =============================================================================
-- In time series forecasting, features fall into two categories:
--
-- KNOWN FEATURES: Available at forecast time (can be computed for future dates)
--   - Calendar features (month, day_of_week, is_holiday)
--   - Planned events (promotions, marketing campaigns)
--   - Cyclical patterns (season indicators)
--
-- UNKNOWN FEATURES: Not available at forecast time
--   - Weather data (temperature, precipitation)
--   - Economic indicators (actual GDP, unemployment)
--   - Competitor actions
--   - Actual sales/demand values
--
-- When backtesting with cross-validation, unknown features in the test set
-- must be handled carefully to avoid data leakage.
--
-- ts_fill_unknown() provides three strategies:
--   1. 'last_value' - Forward fill from last known value (default)
--   2. 'null' - Set to NULL (exclude from model or use model defaults)
--   3. 'default' - Use a constant fill value
--
-- Prerequisites: Run 01_sample_data.sql first
-- =============================================================================

LOAD anofox_forecast;

-- =============================================================================
-- Step 1: Identify Known vs Unknown Features in Our Dataset
-- =============================================================================

-- Our backtest_sample has:
-- - sales: TARGET (what we're predicting)
-- - promotion: KNOWN (planned in advance)
-- - temperature: UNKNOWN (not available until observed)
-- - date: can derive KNOWN calendar features

SELECT
    'promotion' AS feature,
    'Known' AS type,
    'Planned promotions are scheduled in advance' AS reason
UNION ALL SELECT 'month', 'Known', 'Calendar features can be computed for any date'
UNION ALL SELECT 'temperature', 'Unknown', 'Weather not known until observed'
UNION ALL SELECT 'sales', 'Target', 'What we are predicting';

-- =============================================================================
-- Step 2: Basic ts_fill_unknown Usage
-- =============================================================================
-- Fill unknown temperature values beyond a cutoff date

-- Using 'last_value' strategy (default) - forward fill
SELECT
    group_col AS category,
    date_col AS date,
    value_col AS temperature_filled,
    CASE WHEN date_col <= '2023-06-01'::TIMESTAMP THEN 'known' ELSE 'filled' END AS status
FROM ts_fill_unknown(
    'backtest_sample',
    category,
    date,
    temperature,
    '2023-06-01'::DATE,  -- cutoff: values after this are "unknown"
    strategy := 'last_value'
)
WHERE category = 'Apparel'
ORDER BY date_col
LIMIT 24;

-- =============================================================================
-- Step 3: Compare Fill Strategies
-- =============================================================================

-- Create a view of unknown temperature values with different strategies
CREATE OR REPLACE TABLE fill_comparison AS
WITH cutoff AS (SELECT '2023-06-01'::DATE AS dt),
-- Last value strategy
last_val AS (
    SELECT
        group_col AS category,
        date_col AS date,
        value_col AS temp_last_value
    FROM ts_fill_unknown('backtest_sample', category, date, temperature, (SELECT dt FROM cutoff), strategy := 'last_value')
),
-- Null strategy
null_val AS (
    SELECT
        group_col AS category,
        date_col AS date,
        value_col AS temp_null
    FROM ts_fill_unknown('backtest_sample', category, date, temperature, (SELECT dt FROM cutoff), strategy := 'null')
),
-- Default strategy (use average temperature = 60)
default_val AS (
    SELECT
        group_col AS category,
        date_col AS date,
        value_col AS temp_default
    FROM ts_fill_unknown('backtest_sample', category, date, temperature, (SELECT dt FROM cutoff), strategy := 'default', fill_value := 60.0)
)
SELECT
    l.category,
    l.date,
    b.temperature AS temp_actual,
    l.temp_last_value,
    n.temp_null,
    d.temp_default,
    CASE WHEN l.date <= (SELECT dt FROM cutoff) THEN 'known' ELSE 'unknown' END AS period
FROM last_val l
JOIN null_val n ON l.category = n.category AND l.date = n.date
JOIN default_val d ON l.category = d.category AND l.date = d.date
JOIN backtest_sample b ON l.category = b.category AND l.date = b.date;

-- View comparison for unknown period
SELECT
    category,
    date::DATE,
    temp_actual,
    temp_last_value,
    temp_null,
    temp_default,
    period
FROM fill_comparison
WHERE period = 'unknown' AND category = 'Apparel'
ORDER BY date
LIMIT 12;

-- =============================================================================
-- Step 4: Integration with Cross-Validation Splits
-- =============================================================================
-- For each CV fold, we need to fill unknown features in the test set

-- Generate CV folds
CREATE OR REPLACE TABLE cv_fold_times AS
SELECT training_end_times
FROM ts_cv_generate_folds('backtest_sample', date, 3, 3, '1mo');

-- Get fold 1 cutoff date
CREATE OR REPLACE TABLE fold1_cutoff AS
SELECT (SELECT training_end_times FROM cv_fold_times)[1] AS cutoff_date;

-- Fill unknown temperature for fold 1
CREATE OR REPLACE TABLE fold1_data AS
WITH filled AS (
    SELECT
        group_col AS category,
        date_col AS date,
        value_col AS temperature_filled
    FROM ts_fill_unknown(
        'backtest_sample',
        category,
        date,
        temperature,
        (SELECT cutoff_date FROM fold1_cutoff),
        strategy := 'last_value'
    )
)
SELECT
    b.category,
    b.date,
    b.sales,
    b.promotion,  -- Known feature (no filling needed)
    b.temperature AS temperature_actual,
    f.temperature_filled,
    CASE
        WHEN b.date <= (SELECT cutoff_date FROM fold1_cutoff) THEN 'train'
        ELSE 'test'
    END AS split
FROM backtest_sample b
JOIN filled f ON b.category = f.category AND b.date = f.date;

-- View how temperature was filled in test set
SELECT
    category,
    date::DATE,
    split,
    temperature_actual,
    temperature_filled,
    CASE WHEN temperature_actual = temperature_filled THEN 'same' ELSE 'filled' END AS fill_status
FROM fold1_data
WHERE date >= '2023-05-01' AND date <= '2023-11-01' AND category = 'Apparel'
ORDER BY date;

-- =============================================================================
-- Step 5: Handle Multiple Unknown Features
-- =============================================================================
-- If you have multiple unknown features, apply ts_fill_unknown to each

-- Example: Suppose we also had "competitor_price" as unknown
-- We'd fill each unknown feature separately, then join

-- For our dataset, only temperature is unknown
-- Known features don't need filling:
CREATE OR REPLACE TABLE cv_data_prepared AS
SELECT
    f.category,
    f.date,
    f.sales,
    -- Known features (use directly)
    f.promotion,
    EXTRACT(MONTH FROM f.date)::INT AS month,
    SIN(2 * PI() * (EXTRACT(MONTH FROM f.date) - 1) / 12) AS month_sin,
    COS(2 * PI() * (EXTRACT(MONTH FROM f.date) - 1) / 12) AS month_cos,
    -- Unknown features (filled)
    f.temperature_filled AS temperature,
    f.split
FROM fold1_data f;

-- Summary of prepared data
SELECT
    split,
    COUNT(*) AS n_rows,
    ROUND(AVG(sales), 2) AS avg_sales,
    ROUND(AVG(temperature), 2) AS avg_temp_filled,
    SUM(promotion) AS n_promotions
FROM cv_data_prepared
GROUP BY split
ORDER BY split DESC;

-- =============================================================================
-- Step 6: Using ts_mark_unknown for Custom Scenarios
-- =============================================================================
-- ts_mark_unknown adds is_unknown flag without filling values
-- Useful when you want custom fill logic
-- Signature: ts_mark_unknown(source, group_col, date_col, cutoff_date)

SELECT
    category,
    date,
    temperature,
    is_unknown,
    last_known_date::DATE
FROM ts_mark_unknown(
    'backtest_sample',
    category,
    date,
    '2023-06-01'::DATE
)
WHERE category = 'Apparel'
ORDER BY date
LIMIT 24;

-- =============================================================================
-- Step 7: Best Practices Summary
-- =============================================================================

SELECT 'Strategy' AS topic, 'Recommendation' AS guidance
UNION ALL SELECT '---', '---'
UNION ALL SELECT 'last_value', 'Good for slowly-changing features (e.g., recent temperature)'
UNION ALL SELECT 'null', 'Use when model can handle NULLs or you want to exclude feature'
UNION ALL SELECT 'default', 'Use historical mean/median for stationary features'
UNION ALL SELECT '---', '---'
UNION ALL SELECT 'Known features', 'No filling needed - use actual/planned values'
UNION ALL SELECT 'Unknown features', 'Must fill to avoid data leakage in backtesting'
UNION ALL SELECT 'Target variable', 'Never fill - this is what you predict';
