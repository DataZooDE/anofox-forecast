-- =============================================================================
-- Sample Dataset for Peak Detection Example
-- =============================================================================
-- This dataset simulates various time series patterns useful for demonstrating
-- peak detection and timing analysis:
--   - Daily website traffic with clear daily/weekly patterns
--   - Sensor readings with irregular peaks (anomalies)
--   - Monthly sales with seasonal peaks
-- =============================================================================

-- Dataset 1: Website Traffic (hourly, 7 days)
-- Clear daily pattern: peaks during business hours, troughs at night
CREATE OR REPLACE TABLE website_traffic AS
SELECT
    '2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour') AS timestamp,
    i + 1 AS hour_index,
    EXTRACT(HOUR FROM ('2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour')))::INT AS hour_of_day,
    EXTRACT(DOW FROM ('2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour')))::INT AS day_of_week,
    -- Traffic pattern: peaks at 10am and 3pm, low at night
    ROUND(
        1000.0  -- base traffic
        + 800.0 * EXP(-0.5 * POWER((EXTRACT(HOUR FROM ('2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour'))) - 10) / 2.0, 2))  -- morning peak
        + 600.0 * EXP(-0.5 * POWER((EXTRACT(HOUR FROM ('2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour'))) - 15) / 2.0, 2))  -- afternoon peak
        - 700.0 * CASE WHEN EXTRACT(HOUR FROM ('2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour'))) BETWEEN 0 AND 5 THEN 1 ELSE 0 END  -- night dip
        + (i % 17 - 8) * 20  -- small variation
        + CASE WHEN EXTRACT(DOW FROM ('2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour'))) IN (0, 6) THEN -300 ELSE 0 END  -- weekend reduction
    , 0)::INT AS visitors
FROM generate_series(0, 167) AS t(i);  -- 168 hours = 7 days


-- Dataset 2: Sensor Readings (with anomaly peaks)
-- Industrial sensor with normal fluctuations and occasional spike anomalies
CREATE OR REPLACE TABLE sensor_readings AS
SELECT
    '2024-01-01'::DATE + (i * INTERVAL '1 minute') AS timestamp,
    i + 1 AS reading_index,
    -- Normal sensor variation with occasional anomaly spikes
    ROUND(
        50.0  -- baseline
        + 5.0 * SIN(i * 0.1)  -- regular oscillation
        + 3.0 * SIN(i * 0.037)  -- secondary frequency
        + CASE  -- anomaly spikes at specific points
            WHEN i IN (45, 120, 250, 380, 520) THEN 35.0
            WHEN i IN (46, 121, 251, 381, 521) THEN 15.0  -- decay after spike
            ELSE 0.0
        END
        + (i % 7 - 3) * 0.5  -- small noise
    , 2) AS temperature
FROM generate_series(0, 599) AS t(i);  -- 600 readings = 10 hours


-- Dataset 3: Monthly Sales (seasonal peaks)
-- Retail sales with clear yearly seasonality - peaks in December, troughs in summer
CREATE OR REPLACE TABLE monthly_sales AS
SELECT
    '2020-01-01'::DATE + ((i) * INTERVAL '1 month') AS date,
    i + 1 AS month_index,
    EXTRACT(MONTH FROM ('2020-01-01'::DATE + ((i) * INTERVAL '1 month')))::INT AS month_of_year,
    EXTRACT(YEAR FROM ('2020-01-01'::DATE + ((i) * INTERVAL '1 month')))::INT AS year,
    -- Sales pattern: December peak, summer trough, gradual trend
    ROUND(
        100000.0  -- base sales
        + i * 500  -- growth trend
        + 60000.0 * CASE EXTRACT(MONTH FROM ('2020-01-01'::DATE + ((i) * INTERVAL '1 month')))::INT
            WHEN 12 THEN 1.0    -- December peak
            WHEN 11 THEN 0.5    -- November buildup
            WHEN 1 THEN 0.2     -- January clearance
            WHEN 7 THEN -0.4    -- July trough
            WHEN 8 THEN -0.3    -- August trough
            ELSE 0.0
        END
        + (i % 11 - 5) * 2000  -- variation
    , 0)::INT AS sales
FROM generate_series(0, 59) AS t(i);  -- 60 months = 5 years


-- Dataset 4: Heart Rate Monitor (physiological peaks)
-- Simulated heart rate data with clear R-peaks (heartbeats)
CREATE OR REPLACE TABLE heart_rate AS
SELECT
    i AS sample_index,
    i * 0.01 AS time_seconds,  -- 100 Hz sampling
    -- Simplified ECG-like pattern with R-peaks
    ROUND(
        CASE
            -- R-peak every ~100 samples (1 second, ~60 bpm)
            WHEN i % 100 BETWEEN 48 AND 52 THEN
                80.0 + 40.0 * (1.0 - ABS(i % 100 - 50) / 2.0)  -- sharp peak
            WHEN i % 100 BETWEEN 40 AND 47 THEN
                80.0 - 5.0  -- P-wave (small dip before)
            WHEN i % 100 BETWEEN 53 AND 60 THEN
                80.0 - 10.0  -- T-wave (recovery)
            ELSE
                80.0 + (i % 7 - 3) * 0.5  -- baseline with noise
        END
    , 2) AS signal
FROM generate_series(0, 999) AS t(i);  -- 10 seconds of data


-- Verify datasets
SELECT 'website_traffic' AS dataset, COUNT(*) AS rows,
       MIN(visitors) AS min_val, MAX(visitors) AS max_val
FROM website_traffic
UNION ALL
SELECT 'sensor_readings', COUNT(*), MIN(temperature), MAX(temperature)
FROM sensor_readings
UNION ALL
SELECT 'monthly_sales', COUNT(*), MIN(sales), MAX(sales)
FROM monthly_sales
UNION ALL
SELECT 'heart_rate', COUNT(*), MIN(signal), MAX(signal)
FROM heart_rate;
