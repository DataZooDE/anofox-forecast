-- ============================================================================
-- Forecasting Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates time series forecasting with the anofox-forecast
-- extension using *_by table macros.
--
-- Run: ./build/release/duckdb < examples/forecasting/synthetic_forecasting_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

-- Enable progress bar for long operations
SET enable_progress_bar = true;

.print '============================================================================='
.print 'FORECASTING EXAMPLES - Using *_by Table Macros'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Forecasting for Multiple Series
-- ============================================================================
-- Use ts_forecast_by to generate forecasts for grouped time series.

.print ''
.print '>>> SECTION 1: Basic Multi-Series Forecasting'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data
CREATE OR REPLACE TABLE multi_series AS
SELECT * FROM (
    -- Product A: Trend + weekly seasonality
    SELECT
        'Product_A' AS product_id,
        '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
        100 + i * 0.3 + 15 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 8 AS quantity
    FROM generate_series(0, 99) AS t(i)
    UNION ALL
    -- Product B: Higher base, stronger trend
    SELECT
        'Product_B' AS product_id,
        '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
        200 + i * 0.5 + 20 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 10 AS quantity
    FROM generate_series(0, 99) AS t(i)
    UNION ALL
    -- Product C: Lower volume, weaker pattern
    SELECT
        'Product_C' AS product_id,
        '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
        50 + i * 0.2 + 10 * COS(2 * PI() * i / 7) + (RANDOM() - 0.5) * 5 AS quantity
    FROM generate_series(0, 99) AS t(i)
);

.print 'Series counts:'
SELECT product_id, COUNT(*) AS n_rows FROM multi_series GROUP BY product_id ORDER BY product_id;

-- 1.1: Basic forecast with ETS
.print ''
.print 'Section 1.1: Forecast with ETS (7 periods, all products):'
SELECT * FROM ts_forecast_by('multi_series', product_id, date, quantity, 'ETS', 7, MAP{})
ORDER BY id, date;

-- 1.2: Forecast with AutoETS (automatic model selection)
.print ''
.print 'Section 1.2: Forecast with AutoETS (automatic selection):'
SELECT * FROM ts_forecast_by('multi_series', product_id, date, quantity, 'AutoETS', 7, MAP{})
ORDER BY id, date;

-- ============================================================================
-- SECTION 2: Model Comparison
-- ============================================================================

.print ''
.print '>>> SECTION 2: Model Comparison'
.print '-----------------------------------------------------------------------------'

-- Create trend + seasonal data for comparison
CREATE OR REPLACE TABLE comparison_series AS
SELECT
    'series_1' AS id,
    '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
    100 + i * 0.8 + 25 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 5 AS value
FROM generate_series(0, 59) AS t(i);

.print 'Section 2.1: Naive model (baseline):'
SELECT * FROM ts_forecast_by('comparison_series', id, date, value, 'Naive', 7, MAP{});

.print ''
.print 'Section 2.2: SeasonalNaive model (period=7):'
SELECT * FROM ts_forecast_by('comparison_series', id, date, value, 'SeasonalNaive', 7, MAP{'seasonal_period': '7'});

.print ''
.print 'Section 2.3: AutoETS model (automatic selection):'
SELECT * FROM ts_forecast_by('comparison_series', id, date, value, 'AutoETS', 7, MAP{});

.print ''
.print 'Section 2.4: AutoARIMA model (automatic selection):'
SELECT * FROM ts_forecast_by('comparison_series', id, date, value, 'AutoARIMA', 7, MAP{});

-- ============================================================================
-- SECTION 3: Seasonal Models
-- ============================================================================

.print ''
.print '>>> SECTION 3: Seasonal Models'
.print '-----------------------------------------------------------------------------'

-- Create strongly seasonal data
CREATE OR REPLACE TABLE seasonal_data AS
SELECT
    'store_1' AS store_id,
    '2024-01-01'::DATE + INTERVAL (i) DAY AS week,
    500 + i * 2 + 100 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 20 AS revenue
FROM generate_series(0, 55) AS t(i);

.print 'Section 3.1: Holt-Winters (seasonal_period=7):'
SELECT * FROM ts_forecast_by('seasonal_data', store_id, week, revenue, 'HoltWinters', 14, MAP{'seasonal_period': '7'});

.print ''
.print 'Section 3.2: SeasonalES (seasonal_period=7):'
SELECT * FROM ts_forecast_by('seasonal_data', store_id, week, revenue, 'SeasonalES', 14, MAP{'seasonal_period': '7'});

-- ============================================================================
-- SECTION 4: Multiple Seasonality (MSTL)
-- ============================================================================

.print ''
.print '>>> SECTION 4: Multiple Seasonality (MSTL)'
.print '-----------------------------------------------------------------------------'

-- Create data with dual seasonality (daily + weekly patterns)
CREATE OR REPLACE TABLE dual_seasonal AS
SELECT
    'sensor_1' AS sensor_id,
    '2024-01-01 00:00:00'::TIMESTAMP + INTERVAL (i) HOUR AS timestamp,
    100
    + 30 * SIN(2 * PI() * i / 24)      -- daily pattern (period=24 hours)
    + 15 * SIN(2 * PI() * i / 168)     -- weekly pattern (period=168 hours)
    + (RANDOM() - 0.5) * 10 AS reading
FROM generate_series(0, 503) AS t(i);  -- 3 weeks of hourly data

.print 'Input summary:'
SELECT sensor_id, COUNT(*) AS n_hours, MIN(timestamp) AS start, MAX(timestamp) AS end
FROM dual_seasonal GROUP BY sensor_id;

.print ''
.print 'Section 4.1: MSTL forecast (daily + weekly seasonality):'
SELECT * FROM ts_forecast_by('dual_seasonal', sensor_id, timestamp, reading, 'MSTL', 48, MAP{'seasonal_periods': '[24, 168]'})
LIMIT 10;

-- ============================================================================
-- SECTION 5: Intermittent Demand Models
-- ============================================================================

.print ''
.print '>>> SECTION 5: Intermittent Demand Models'
.print '-----------------------------------------------------------------------------'

-- Create sparse demand data (many zeros)
CREATE OR REPLACE TABLE spare_parts AS
SELECT
    'SKU_001' AS sku,
    '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
    CASE
        WHEN RANDOM() < 0.3 THEN FLOOR(1 + RANDOM() * 5)::INTEGER  -- 30% chance of demand
        ELSE 0
    END AS demand
FROM generate_series(0, 89) AS t(i);

.print 'Demand distribution:'
SELECT sku, COUNT(*) AS total_days,
       SUM(CASE WHEN demand = 0 THEN 1 ELSE 0 END) AS zero_days,
       SUM(CASE WHEN demand > 0 THEN 1 ELSE 0 END) AS demand_days,
       ROUND(AVG(demand), 2) AS avg_demand
FROM spare_parts GROUP BY sku;

.print ''
.print 'Section 5.1: CrostonClassic forecast:'
SELECT * FROM ts_forecast_by('spare_parts', sku, date, demand, 'CrostonClassic', 14, MAP{});

.print ''
.print 'Section 5.2: CrostonSBA forecast (Syntetos-Boylan Approximation):'
SELECT * FROM ts_forecast_by('spare_parts', sku, date, demand, 'CrostonSBA', 14, MAP{});

.print ''
.print 'Section 5.3: ADIDA forecast (Aggregate-Disaggregate):'
SELECT * FROM ts_forecast_by('spare_parts', sku, date, demand, 'ADIDA', 14, MAP{});

-- ============================================================================
-- SECTION 6: Exogenous Variables
-- ============================================================================

.print ''
.print '>>> SECTION 6: Exogenous Variables'
.print '-----------------------------------------------------------------------------'

-- Create historical data with external variables
CREATE OR REPLACE TABLE sales_with_features AS
SELECT
    'store_1' AS store_id,
    '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
    100 + i * 0.5 + 15 * temp + 30 * promo + (RANDOM() - 0.5) * 10 AS amount,
    temp,
    promo
FROM (
    SELECT
        i,
        0.8 + (RANDOM() - 0.5) * 0.4 AS temp,  -- temperature factor
        CASE WHEN RANDOM() < 0.2 THEN 1 ELSE 0 END AS promo  -- 20% promo days
    FROM generate_series(0, 59) AS t(i)
) AS data;

-- Create future exogenous values (known in advance)
CREATE OR REPLACE TABLE future_features AS
SELECT
    'store_1' AS store_id,
    '2024-01-01'::DATE + INTERVAL (60 + i) DAY AS date,
    0.9 + (RANDOM() - 0.5) * 0.3 AS temp,     -- forecast temperature
    CASE WHEN i IN (2, 5) THEN 1 ELSE 0 END AS promo  -- planned promotions
FROM generate_series(0, 6) AS t(i);

.print 'Historical data (last 5 rows):'
SELECT * FROM sales_with_features ORDER BY date DESC LIMIT 5;

.print ''
.print 'Future features:'
SELECT * FROM future_features ORDER BY date;

.print ''
.print 'Section 6.1: Forecast with exogenous variables (AutoARIMA):'
SELECT * FROM ts_forecast_exog_by(
    'sales_with_features',
    store_id,
    date,
    amount,
    ['temp', 'promo'],
    'future_features',
    date,
    ['temp', 'promo'],
    'AutoARIMA',
    7,
    MAP{},
    '1d'
);

-- ============================================================================
-- SECTION 7: Comparing Forecast Quality
-- ============================================================================

.print ''
.print '>>> SECTION 7: Comparing Forecast Quality'
.print '-----------------------------------------------------------------------------'

-- Create data to compare forecast results
CREATE OR REPLACE TABLE forecast_comparison AS
SELECT * FROM (
    -- Series with clear weekly pattern
    SELECT
        'weekly_pattern' AS series_id,
        '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
        100 + 50 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 10 AS value
    FROM generate_series(0, 69) AS t(i)
    UNION ALL
    -- Series with trend only (no seasonality)
    SELECT
        'trend_only' AS series_id,
        '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
        50 + i * 1.0 + (RANDOM() - 0.5) * 15 AS value
    FROM generate_series(0, 69) AS t(i)
    UNION ALL
    -- Random series (no pattern)
    SELECT
        'random' AS series_id,
        '2024-01-01'::DATE + INTERVAL (i) DAY AS date,
        100 + (RANDOM() - 0.5) * 40 AS value
    FROM generate_series(0, 69) AS t(i)
);

.print 'Section 7.1: AutoETS on different pattern types:'
SELECT
    id,
    COUNT(*) AS n_forecasts,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    ROUND(AVG(upper_90 - lower_90), 2) AS avg_interval_width
FROM ts_forecast_by('forecast_comparison', series_id, date, value, 'AutoETS', 7, MAP{}, '1d')
GROUP BY id
ORDER BY id;

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS multi_series;
DROP TABLE IF EXISTS comparison_series;
DROP TABLE IF EXISTS seasonal_data;
DROP TABLE IF EXISTS dual_seasonal;
DROP TABLE IF EXISTS spare_parts;
DROP TABLE IF EXISTS sales_with_features;
DROP TABLE IF EXISTS future_features;
DROP TABLE IF EXISTS forecast_comparison;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'FORECASTING EXAMPLES COMPLETE'
.print '============================================================================='
