-- ============================================================================
-- Synthetic Peak Detection Examples
-- ============================================================================
-- This file demonstrates peak detection and timing analysis using synthetic
-- (generated) data. Use this to learn the API before applying to your datasets.
--
-- Patterns included:
--   1. Basic Peak Detection - Find local maxima with ts_detect_peaks
--   2. Peak Timing Analysis - Analyze timing consistency with ts_analyze_peak_timing
--
-- Prerequisites:
--   - anofox_forecast extension loaded
-- ============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- PATTERN 1: Basic Peak Detection
-- ============================================================================
-- Scenario: Detect local maxima in time series data
-- Use cases: Demand peaks, anomaly spikes, seasonal highs, heartbeats

SELECT
    '=== Pattern 1: Basic Peak Detection ===' AS section;

-- Generate sample data: Hourly website traffic (7 days)
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

-- Section 1.1: Detect all peaks (no filtering)
SELECT 'Section 1.1: All Peaks (unfiltered)' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_detect_peaks(values) AS detection
    FROM traffic_array
)
SELECT
    detection.n_peaks AS total_peaks,
    ROUND(detection.mean_period, 2) AS avg_period_between_peaks,
    detection.peaks[1:3] AS first_3_peaks
FROM result;

-- Section 1.2: Filter by prominence (significant peaks only)
SELECT 'Section 1.2: High Prominence Peaks (>200)' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_detect_peaks(values, 200.0) AS detection
    FROM traffic_array
)
SELECT
    detection.n_peaks AS significant_peaks,
    detection.peaks AS all_peaks,
    detection.inter_peak_distances AS distances_between_peaks
FROM result;

-- Section 1.3: Filter by prominence only (for daily peaks)
SELECT 'Section 1.3: Daily Peaks (Prominence > 0.5)' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_detect_peaks(values, 0.5) AS detection
    FROM traffic_array
)
SELECT
    detection.n_peaks AS daily_peaks,
    detection.peaks AS peaks,
    ROUND(detection.mean_period, 2) AS mean_period
FROM result;

-- Section 1.4: Map peak indices back to timestamps
SELECT 'Section 1.4: Peaks with Timestamps' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_detect_peaks(values, 300.0) AS detection
    FROM traffic_array
),
peaks_unnested AS (
    SELECT UNNEST(detection.peaks) AS peak
    FROM result
)
SELECT
    peak.index AS peak_index,
    w.timestamp AS peak_time,
    EXTRACT(HOUR FROM w.timestamp) AS hour_of_day,
    EXTRACT(DOW FROM w.timestamp) AS day_of_week,
    w.visitors AS actual_visitors,
    ROUND(peak.prominence, 1) AS prominence
FROM peaks_unnested p
JOIN website_traffic w ON w.hour_index = p.peak.index + 1  -- +1 because indices are 0-based
ORDER BY w.timestamp;

-- Section 1.5: Anomaly detection in sensor data
SELECT 'Section 1.5: Anomaly Spike Detection' AS step;

-- Generate sensor data with anomaly spikes
CREATE OR REPLACE TABLE sensor_readings AS
SELECT
    '2024-01-01'::DATE + (i * INTERVAL '1 minute') AS timestamp,
    i + 1 AS reading_index,
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

WITH sensor_array AS (
    SELECT LIST(temperature ORDER BY timestamp) AS values
    FROM sensor_readings
),
result AS (
    SELECT ts_detect_peaks(values) AS detection
    FROM sensor_array
),
peaks_unnested AS (
    SELECT UNNEST(detection.peaks) AS peak
    FROM result
)
SELECT
    COUNT(*) AS anomaly_count,
    LIST(peak ORDER BY peak.index) AS anomalies
FROM peaks_unnested
WHERE peak.prominence > 0.6;  -- Filter for high-prominence anomalies only

-- Section 1.6: Seasonal peaks in sales data
SELECT 'Section 1.6: Seasonal Peak Detection (Monthly Sales)' AS step;

-- Generate monthly sales data with yearly seasonality (5 years)
CREATE OR REPLACE TABLE monthly_sales AS
SELECT
    '2020-01-01'::DATE + ((i) * INTERVAL '1 month') AS date,
    i + 1 AS month_index,
    EXTRACT(MONTH FROM ('2020-01-01'::DATE + ((i) * INTERVAL '1 month')))::INT AS month_of_year,
    EXTRACT(YEAR FROM ('2020-01-01'::DATE + ((i) * INTERVAL '1 month')))::INT AS year,
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
FROM generate_series(0, 59) AS t(i);

WITH sales_array AS (
    SELECT LIST(sales ORDER BY date) AS values
    FROM monthly_sales
),
result AS (
    SELECT ts_detect_peaks(values, 0.2) AS detection  -- prominence > 0.2 for yearly peaks
    FROM sales_array
),
peaks_unnested AS (
    SELECT UNNEST(detection.peaks) AS peak
    FROM result
)
SELECT
    p.peak.index AS peak_index,
    s.date AS peak_month,
    s.month_of_year,
    s.year,
    s.sales AS peak_sales,
    ROUND(p.peak.prominence, 4) AS prominence
FROM peaks_unnested p
JOIN monthly_sales s ON s.month_index = p.peak.index + 1
ORDER BY s.date;

-- Section 1.7: Heart rate R-peak detection
SELECT 'Section 1.7: Heart Rate R-Peak Detection' AS step;

-- Generate simulated ECG-like signal (clean, no baseline noise)
CREATE OR REPLACE TABLE heart_rate AS
SELECT
    i AS sample_index,
    i * 0.01 AS time_seconds,  -- 100 Hz sampling
    ROUND(
        CASE
            -- R-peak every ~100 samples (1 second, ~60 bpm)
            WHEN i % 100 BETWEEN 48 AND 52 THEN
                80.0 + 40.0 * (1.0 - ABS(i % 100 - 50) / 2.0)  -- sharp peak
            ELSE
                80.0  -- flat baseline
        END
    , 2) AS signal
FROM generate_series(0, 999) AS t(i);  -- 10 seconds of data

WITH heart_array AS (
    SELECT LIST(signal ORDER BY sample_index) AS values
    FROM heart_rate
),
result AS (
    SELECT ts_detect_peaks(values, 0.3) AS detection  -- prominence > 0.3 for R-peaks
    FROM heart_array
)
SELECT
    detection.n_peaks AS heartbeat_count,
    ROUND(detection.n_peaks / 10.0 * 60, 0) AS estimated_bpm,  -- 10 seconds of data
    detection.peaks[1:5] AS first_5_beats,
    ROUND(detection.mean_period, 1) AS avg_samples_between_beats
FROM result;

-- Section 1.8: Parameter sensitivity analysis
SELECT 'Section 1.8: Parameter Sensitivity Analysis' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
)
SELECT
    'All peaks' AS filter_type,
    (ts_detect_peaks(values)).n_peaks AS peak_count
FROM traffic_array
UNION ALL
SELECT
    'Prominence > 0.1',
    (ts_detect_peaks(values, 0.1)).n_peaks
FROM traffic_array
UNION ALL
SELECT
    'Prominence > 0.3',
    (ts_detect_peaks(values, 0.3)).n_peaks
FROM traffic_array
UNION ALL
SELECT
    'Prominence > 0.5',
    (ts_detect_peaks(values, 0.5)).n_peaks
FROM traffic_array
UNION ALL
SELECT
    'Prom > 0.1, dist > 6',
    (ts_detect_peaks(values, 0.1, 6)).n_peaks
FROM traffic_array
UNION ALL
SELECT
    'Prom > 0.1, dist > 12',
    (ts_detect_peaks(values, 0.1, 12)).n_peaks
FROM traffic_array;

-- ============================================================================
-- PATTERN 2: Peak Timing Analysis
-- ============================================================================
-- Scenario: Analyze when peaks occur within seasonal cycles
-- Use cases: Demand planning, capacity scheduling, pattern consistency

SELECT
    '=== Pattern 2: Peak Timing Analysis ===' AS section;

-- Section 2.1: Analyze daily peak timing in website traffic
SELECT 'Section 2.1: Daily Peak Timing Consistency' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_analyze_peak_timing(values, 24) AS timing
    FROM traffic_array
)
SELECT
    timing.n_peaks AS peaks_detected,
    ROUND(timing.mean_timing * 24, 1) AS mean_peak_hour,
    ROUND(timing.std_timing * 24, 2) AS peak_hour_std,
    ROUND(timing.variability_score, 3) AS variability_score,
    timing.is_stable AS stable_timing,
    CASE
        WHEN timing.variability_score < 0.1 THEN 'Very Consistent'
        WHEN timing.variability_score < 0.3 THEN 'Consistent'
        WHEN timing.variability_score < 0.5 THEN 'Moderate Variability'
        ELSE 'High Variability'
    END AS timing_assessment
FROM result;

-- Section 2.2: Yearly peak timing in sales data
SELECT 'Section 2.2: Yearly Peak Timing (Monthly Sales)' AS step;

WITH sales_array AS (
    SELECT LIST(sales ORDER BY date) AS values
    FROM monthly_sales
),
result AS (
    SELECT ts_analyze_peak_timing(values, 12) AS timing
    FROM sales_array
)
SELECT
    timing.n_peaks AS years_with_peaks,
    ROUND(timing.mean_timing * 12, 1) AS mean_peak_month_offset,
    ROUND(timing.mean_timing * 12 + 1, 0)::INT AS typical_peak_month,
    CASE ROUND(timing.mean_timing * 12 + 1, 0)::INT
        WHEN 1 THEN 'January'
        WHEN 2 THEN 'February'
        WHEN 3 THEN 'March'
        WHEN 4 THEN 'April'
        WHEN 5 THEN 'May'
        WHEN 6 THEN 'June'
        WHEN 7 THEN 'July'
        WHEN 8 THEN 'August'
        WHEN 9 THEN 'September'
        WHEN 10 THEN 'October'
        WHEN 11 THEN 'November'
        WHEN 12 THEN 'December'
        ELSE 'Unknown'
    END AS typical_peak_month_name,
    ROUND(timing.std_timing * 12, 2) AS peak_month_std,
    ROUND(timing.variability_score, 3) AS variability,
    timing.is_stable AS consistent_yearly_peaks
FROM result;

-- Section 2.3: Heart rate regularity analysis
SELECT 'Section 2.3: Heartbeat Timing Regularity' AS step;

WITH heart_array AS (
    SELECT LIST(signal ORDER BY sample_index) AS values
    FROM heart_rate
),
result AS (
    SELECT ts_analyze_peak_timing(values, 100) AS timing
    FROM heart_array
)
SELECT
    timing.n_peaks AS beats_detected,
    ROUND(timing.mean_timing * 100, 1) AS mean_beat_position,
    ROUND(timing.std_timing * 100, 2) AS position_std,
    ROUND(timing.variability_score, 4) AS variability,
    timing.is_stable AS regular_rhythm,
    CASE
        WHEN timing.variability_score < 0.1 THEN 'Very Regular'
        WHEN timing.variability_score < 0.3 THEN 'Regular'
        WHEN timing.variability_score < 0.5 THEN 'Slightly Irregular'
        ELSE 'Irregular'
    END AS rhythm_assessment
FROM result;

-- Section 2.4: Full timing analysis with all metrics
SELECT 'Section 2.4: Full Timing Analysis Details' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_analyze_peak_timing(values, 24) AS timing
    FROM traffic_array
)
SELECT
    timing.n_peaks AS total_peaks,
    timing.peak_times AS peak_times_raw,
    timing.normalized_timing AS normalized_positions,
    ROUND(timing.mean_timing, 4) AS mean_normalized_timing,
    ROUND(timing.std_timing, 4) AS std_normalized_timing,
    ROUND(timing.range_timing, 4) AS timing_range,
    ROUND(timing.variability_score, 4) AS variability_score,
    ROUND(timing.timing_trend, 4) AS timing_trend,
    timing.is_stable AS is_stable
FROM result;

-- Section 2.5: Timing trend analysis
SELECT 'Section 2.5: Peak Timing Trend (Drift Detection)' AS step;

WITH sales_array AS (
    SELECT LIST(sales ORDER BY date) AS values
    FROM monthly_sales
),
result AS (
    SELECT ts_analyze_peak_timing(values, 12) AS timing
    FROM sales_array
)
SELECT
    timing.n_peaks AS cycles_analyzed,
    ROUND(timing.timing_trend, 4) AS timing_trend,
    ROUND(timing.timing_trend * 12, 2) AS trend_months_per_year,
    CASE
        WHEN timing.timing_trend > 0.02 THEN 'Peaks shifting LATER over time'
        WHEN timing.timing_trend < -0.02 THEN 'Peaks shifting EARLIER over time'
        ELSE 'Stable peak timing'
    END AS trend_interpretation
FROM result;

-- Section 2.6: Combined peak detection and timing analysis
SELECT 'Section 2.6: Combined Detection + Timing Analysis' AS step;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
analysis AS (
    SELECT
        ts_detect_peaks(values, 200.0) AS peaks,
        ts_analyze_peak_timing(values, 24) AS timing
    FROM traffic_array
)
SELECT
    peaks.n_peaks AS significant_peaks_detected,
    timing.n_peaks AS timing_cycles_analyzed,
    ROUND(timing.mean_timing * 24, 1) AS typical_peak_hour,
    ROUND(timing.variability_score, 3) AS timing_variability,
    timing.is_stable AS predictable_timing,
    ROUND(peaks.mean_period, 1) AS avg_hours_between_peaks,
    CASE
        WHEN timing.is_stable AND peaks.n_peaks > 0
            THEN 'Predictable peak pattern - good for planning'
        WHEN NOT timing.is_stable AND peaks.n_peaks > 0
            THEN 'Variable peak timing - monitor closely'
        ELSE 'Insufficient peaks for analysis'
    END AS recommendation
FROM analysis;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT
    '=== Examples Complete ===' AS section;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS website_traffic;
-- DROP TABLE IF EXISTS sensor_readings;
-- DROP TABLE IF EXISTS monthly_sales;
-- DROP TABLE IF EXISTS heart_rate;
