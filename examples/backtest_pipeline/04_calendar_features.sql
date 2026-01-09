-- =============================================================================
-- Add Month/Calendar Features via SQL DATE_PART
-- =============================================================================
-- Calendar features are "known" features - they can be computed for any
-- future date, making them ideal for forecasting with exogenous variables.
--
-- This example shows how to extract various calendar features from dates.
--
-- Prerequisites: Run 01_sample_data.sql first
-- =============================================================================

LOAD anofox_forecast;

-- =============================================================================
-- Step 1: Basic Calendar Feature Extraction
-- =============================================================================
-- Use EXTRACT or DATE_PART to get calendar components

CREATE OR REPLACE TABLE sales_with_calendar AS
SELECT
    category,
    date,
    sales,
    promotion,
    temperature,
    -- Basic calendar features
    EXTRACT(YEAR FROM date)::INT AS year,
    EXTRACT(MONTH FROM date)::INT AS month,
    EXTRACT(DAY FROM date)::INT AS day_of_month,
    EXTRACT(DOW FROM date)::INT AS day_of_week,  -- 0=Sunday, 6=Saturday
    EXTRACT(DOY FROM date)::INT AS day_of_year,
    EXTRACT(WEEK FROM date)::INT AS week_of_year,
    EXTRACT(QUARTER FROM date)::INT AS quarter
FROM backtest_sample;

-- View the calendar features
SELECT * FROM sales_with_calendar
ORDER BY category, date
LIMIT 15;

-- =============================================================================
-- Step 2: Derived Calendar Features
-- =============================================================================
-- Create additional features useful for retail/business forecasting

CREATE OR REPLACE TABLE sales_with_all_calendar AS
SELECT
    *,
    -- Season (Northern Hemisphere)
    CASE
        WHEN month IN (12, 1, 2) THEN 'winter'
        WHEN month IN (3, 4, 5) THEN 'spring'
        WHEN month IN (6, 7, 8) THEN 'summer'
        ELSE 'fall'
    END AS season,
    -- Is weekend (for daily data)
    CASE WHEN day_of_week IN (0, 6) THEN 1 ELSE 0 END AS is_weekend,
    -- Month start/end indicators
    CASE WHEN day_of_month <= 7 THEN 1 ELSE 0 END AS is_month_start,
    CASE WHEN day_of_month >= 24 THEN 1 ELSE 0 END AS is_month_end,
    -- Quarter start/end
    CASE WHEN month IN (1, 4, 7, 10) THEN 1 ELSE 0 END AS is_quarter_start,
    CASE WHEN month IN (3, 6, 9, 12) THEN 1 ELSE 0 END AS is_quarter_end,
    -- Year start/end
    CASE WHEN month = 1 THEN 1 ELSE 0 END AS is_year_start,
    CASE WHEN month = 12 THEN 1 ELSE 0 END AS is_year_end,
    -- Holiday season indicator (Nov-Dec for retail)
    CASE WHEN month IN (11, 12) THEN 1 ELSE 0 END AS is_holiday_season,
    -- Back-to-school season (Aug-Sep)
    CASE WHEN month IN (8, 9) THEN 1 ELSE 0 END AS is_back_to_school
FROM sales_with_calendar;

-- Summary of calendar feature distribution
SELECT
    month,
    season,
    is_holiday_season,
    COUNT(*) AS n_obs,
    ROUND(AVG(sales), 2) AS avg_sales
FROM sales_with_all_calendar
GROUP BY month, season, is_holiday_season
ORDER BY month;

-- =============================================================================
-- Step 3: Cyclical Encoding for Month
-- =============================================================================
-- For models that benefit from cyclical features, encode month as sin/cos
-- This captures the circular nature of time (December is close to January)

CREATE OR REPLACE TABLE sales_with_cyclical AS
SELECT
    category,
    date,
    sales,
    promotion,
    month,
    quarter,
    is_holiday_season,
    -- Cyclical encoding of month (captures Dec-Jan proximity)
    SIN(2 * PI() * (month - 1) / 12) AS month_sin,
    COS(2 * PI() * (month - 1) / 12) AS month_cos,
    -- Cyclical encoding of quarter
    SIN(2 * PI() * (quarter - 1) / 4) AS quarter_sin,
    COS(2 * PI() * (quarter - 1) / 4) AS quarter_cos,
    -- Cyclical encoding of day of year (for daily data)
    SIN(2 * PI() * (EXTRACT(DOY FROM date) - 1) / 365) AS doy_sin,
    COS(2 * PI() * (EXTRACT(DOY FROM date) - 1) / 365) AS doy_cos
FROM sales_with_all_calendar;

-- View cyclical features
SELECT
    category,
    date,
    month,
    ROUND(month_sin, 4) AS month_sin,
    ROUND(month_cos, 4) AS month_cos,
    sales
FROM sales_with_cyclical
WHERE category = 'Apparel'
ORDER BY date
LIMIT 12;

-- =============================================================================
-- Step 4: One-Hot Encoding for Month
-- =============================================================================
-- For models that work better with dummy variables

CREATE OR REPLACE TABLE sales_with_month_dummies AS
SELECT
    category,
    date,
    sales,
    promotion,
    month,
    -- One-hot encoded months (using month 1 as reference)
    CASE WHEN month = 2 THEN 1 ELSE 0 END AS month_feb,
    CASE WHEN month = 3 THEN 1 ELSE 0 END AS month_mar,
    CASE WHEN month = 4 THEN 1 ELSE 0 END AS month_apr,
    CASE WHEN month = 5 THEN 1 ELSE 0 END AS month_may,
    CASE WHEN month = 6 THEN 1 ELSE 0 END AS month_jun,
    CASE WHEN month = 7 THEN 1 ELSE 0 END AS month_jul,
    CASE WHEN month = 8 THEN 1 ELSE 0 END AS month_aug,
    CASE WHEN month = 9 THEN 1 ELSE 0 END AS month_sep,
    CASE WHEN month = 10 THEN 1 ELSE 0 END AS month_oct,
    CASE WHEN month = 11 THEN 1 ELSE 0 END AS month_nov,
    CASE WHEN month = 12 THEN 1 ELSE 0 END AS month_dec
FROM sales_with_calendar;

-- Verify one-hot encoding
SELECT month, month_feb, month_mar, month_apr, month_nov, month_dec
FROM sales_with_month_dummies
WHERE category = 'Apparel'
ORDER BY date
LIMIT 12;

-- =============================================================================
-- Step 5: Combine with CV Splits
-- =============================================================================
-- Join calendar features with cross-validation splits

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

-- Join with calendar features
CREATE OR REPLACE TABLE cv_data_with_calendar AS
SELECT
    cv.category,
    cv.date,
    cv.sales,
    cv.fold_id,
    cv.split,
    cal.promotion,
    cal.month,
    cal.quarter,
    cal.is_holiday_season,
    cal.month_sin,
    cal.month_cos
FROM cv_splits cv
JOIN sales_with_cyclical cal
    ON cv.category = cal.category
    AND cv.date = cal.date;

-- Summary by fold, split, and month
SELECT
    fold_id,
    split,
    month,
    COUNT(*) AS n_obs,
    ROUND(AVG(sales), 2) AS avg_sales
FROM cv_data_with_calendar
GROUP BY fold_id, split, month
ORDER BY fold_id, split DESC, month
LIMIT 20;

-- =============================================================================
-- Step 6: Generate Future Calendar Features
-- =============================================================================
-- Calendar features are "known" - we can generate them for future dates
-- This is essential for forecasting with exogenous variables

-- Generate future dates for the forecast horizon
CREATE OR REPLACE TABLE future_dates AS
SELECT
    category,
    -- Generate 6 months into the future from the last date
    UNNEST(generate_series(
        (SELECT MAX(date) FROM backtest_sample) + INTERVAL '1 month',
        (SELECT MAX(date) FROM backtest_sample) + INTERVAL '6 months',
        INTERVAL '1 month'
    )) AS date
FROM (SELECT DISTINCT category FROM backtest_sample);

-- Add calendar features to future dates
CREATE OR REPLACE TABLE future_with_calendar AS
SELECT
    category,
    date,
    EXTRACT(MONTH FROM date)::INT AS month,
    EXTRACT(QUARTER FROM date)::INT AS quarter,
    CASE WHEN EXTRACT(MONTH FROM date) IN (11, 12) THEN 1 ELSE 0 END AS is_holiday_season,
    SIN(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) AS month_sin,
    COS(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) AS month_cos,
    -- Assume promotions are planned (known feature)
    CASE WHEN EXTRACT(MONTH FROM date) IN (3, 6, 9, 12) THEN 1 ELSE 0 END AS promotion
FROM future_dates;

-- View future calendar features
SELECT
    category,
    date,
    month,
    ROUND(month_sin, 4) AS month_sin,
    ROUND(month_cos, 4) AS month_cos,
    promotion
FROM future_with_calendar
ORDER BY category, date;

-- =============================================================================
-- Summary: Calendar Features for Forecasting
-- =============================================================================
-- Key points:
-- 1. Calendar features are "known" features - available for future dates
-- 2. Use EXTRACT/DATE_PART for basic features (month, day, quarter)
-- 3. Create derived features (season, holiday indicators)
-- 4. Consider cyclical encoding for circular time features
-- 5. One-hot encoding useful for some models
-- 6. Generate future calendar features for the forecast horizon

SELECT
    'Historical observations' AS dataset,
    COUNT(*) AS n_rows,
    COUNT(DISTINCT category) AS n_categories,
    MIN(date)::DATE AS start_date,
    MAX(date)::DATE AS end_date
FROM sales_with_calendar
UNION ALL
SELECT
    'Future dates for forecasting',
    COUNT(*),
    COUNT(DISTINCT category),
    MIN(date)::DATE,
    MAX(date)::DATE
FROM future_with_calendar;
