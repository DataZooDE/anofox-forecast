-- =============================================================================
-- Sample Dataset for Backtest Pipeline Example
-- =============================================================================
-- This dataset simulates monthly retail sales for 2 product categories over 3 years.
-- It includes:
--   - Seasonality (higher in winter/holiday months, lower in summer)
--   - Trend component (gradual growth)
--   - Changepoint around month 18 (business expansion - level shift)
--   - Known features: month (calendar), promotion (planned in advance)
--   - Unknown features: temperature (not available at forecast time)
-- =============================================================================

-- Create sample dataset with realistic retail patterns
CREATE OR REPLACE TABLE backtest_sample AS
WITH base_data AS (
    SELECT
        category,
        date,
        month_num,
        -- Base sales with trend
        base_value + trend_component AS base_with_trend,
        -- Seasonality: higher in winter (Nov-Jan), lower in summer (Jun-Aug)
        CASE
            WHEN month_num IN (11, 12, 1) THEN 1.3   -- Holiday boost
            WHEN month_num IN (2, 3) THEN 1.1        -- Post-holiday
            WHEN month_num IN (6, 7, 8) THEN 0.85   -- Summer dip
            ELSE 1.0
        END AS seasonal_factor,
        -- Changepoint: level shift after month 18 (business expansion)
        CASE WHEN row_num > 18 THEN 50.0 ELSE 0.0 END AS changepoint_effect,
        -- Promotion effect (known in advance - planned promotions)
        promotion,
        CASE WHEN promotion = 1 THEN 25.0 ELSE 0.0 END AS promo_effect,
        -- Temperature (unknown at forecast time - correlated with season)
        temperature,
        -- Temperature effect on sales (cooler = more sales for this retail type)
        (70.0 - temperature) * 0.5 AS temp_effect,
        -- Small random variation (deterministic for reproducibility)
        variation
    FROM (
        SELECT
            cat.category,
            '2022-01-01'::DATE + ((t.i - 1) * INTERVAL '1 month') AS date,
            EXTRACT(MONTH FROM ('2022-01-01'::DATE + ((t.i - 1) * INTERVAL '1 month')))::INT AS month_num,
            t.i AS row_num,
            -- Base sales differ by category
            CASE WHEN cat.category = 'Electronics' THEN 500.0 ELSE 300.0 END AS base_value,
            -- Trend: 2% monthly growth
            t.i * 5.0 AS trend_component,
            -- Planned promotions (known): every 3rd month and December
            CASE
                WHEN EXTRACT(MONTH FROM ('2022-01-01'::DATE + ((t.i - 1) * INTERVAL '1 month'))) = 12 THEN 1
                WHEN t.i % 3 = 0 THEN 1
                ELSE 0
            END AS promotion,
            -- Temperature based on month (Northern hemisphere pattern)
            CASE EXTRACT(MONTH FROM ('2022-01-01'::DATE + ((t.i - 1) * INTERVAL '1 month')))::INT
                WHEN 1 THEN 35.0 WHEN 2 THEN 38.0 WHEN 3 THEN 48.0
                WHEN 4 THEN 58.0 WHEN 5 THEN 68.0 WHEN 6 THEN 78.0
                WHEN 7 THEN 82.0 WHEN 8 THEN 80.0 WHEN 9 THEN 72.0
                WHEN 10 THEN 60.0 WHEN 11 THEN 48.0 WHEN 12 THEN 38.0
            END + (cat.cat_offset * 2) AS temperature,  -- slight category variation
            -- Deterministic variation based on position
            ((t.i * 7 + cat.cat_offset * 13) % 20 - 10)::DOUBLE AS variation
        FROM generate_series(1, 36) AS t(i)
        CROSS JOIN (
            SELECT 'Electronics' AS category, 0 AS cat_offset
            UNION ALL SELECT 'Apparel', 1
        ) AS cat
    ) raw
)
SELECT
    category,
    date,
    month_num,
    -- Final sales value combining all effects
    ROUND(
        (base_with_trend * seasonal_factor)
        + changepoint_effect
        + promo_effect
        + temp_effect
        + variation,
        2
    ) AS sales,
    -- Features
    promotion,           -- Known feature (planned in advance)
    temperature          -- Unknown feature (not available at forecast time)
FROM base_data
ORDER BY category, date;

-- Verify the dataset
SELECT
    category,
    COUNT(*) AS n_rows,
    MIN(date) AS start_date,
    MAX(date) AS end_date,
    ROUND(AVG(sales), 2) AS avg_sales,
    ROUND(MIN(sales), 2) AS min_sales,
    ROUND(MAX(sales), 2) AS max_sales
FROM backtest_sample
GROUP BY category
ORDER BY category;
