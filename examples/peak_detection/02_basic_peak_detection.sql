-- =============================================================================
-- Basic Peak Detection with ts_detect_peaks
-- =============================================================================
-- This example demonstrates how to detect peaks (local maxima) in time series
-- data using various parameter configurations.
--
-- Key concepts:
--   - Peak detection identifies local maxima in time series
--   - Prominence measures how much a peak stands out from surrounding data
--   - min_distance prevents detecting multiple peaks too close together
-- =============================================================================

-- Load the sample data first:
-- .read examples/peak_detection/01_sample_data.sql

-- -----------------------------------------------------------------------------
-- Example 1: Simple peak detection (all peaks)
-- -----------------------------------------------------------------------------
-- Detect all peaks in website traffic data without filtering

SELECT 'Website Traffic - All Peaks' AS example;

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
    detection.mean_period AS avg_period_between_peaks,
    detection.peaks[1:3] AS first_3_peaks  -- Each peak has: index, time, value, prominence
FROM result;


-- -----------------------------------------------------------------------------
-- Example 2: Filter by prominence (significant peaks only)
-- -----------------------------------------------------------------------------
-- Higher prominence = more significant peaks that stand out more clearly

SELECT 'Website Traffic - High Prominence Peaks (>200)' AS example;

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


-- -----------------------------------------------------------------------------
-- Example 3: Control peak spacing with min_distance
-- -----------------------------------------------------------------------------
-- Ensure peaks are at least N observations apart
-- Useful for preventing detection of minor fluctuations

SELECT 'Website Traffic - Peaks at least 12 hours apart' AS example;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_detect_peaks(values, 100.0, 12) AS detection
    FROM traffic_array
)
SELECT
    detection.n_peaks AS spaced_peaks,
    detection.peaks AS peaks,
    detection.mean_period AS mean_period
FROM result;


-- -----------------------------------------------------------------------------
-- Example 4: Map peak indices back to original timestamps
-- -----------------------------------------------------------------------------
-- Convert detected peak indices to actual timestamps for interpretation

SELECT 'Website Traffic - Peaks with Timestamps' AS example;

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


-- -----------------------------------------------------------------------------
-- Example 5: Anomaly detection in sensor data
-- -----------------------------------------------------------------------------
-- Use peak detection to find anomaly spikes in sensor readings
-- Filter for high prominence (>0.6) to identify true anomalies vs normal fluctuations

SELECT 'Sensor Readings - Anomaly Spike Detection' AS example;

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


-- -----------------------------------------------------------------------------
-- Example 6: Seasonal peaks in sales data
-- -----------------------------------------------------------------------------
-- Detect yearly peaks (December holiday season)
-- Note: Prominence is normalized (0-1 scale), so use values like 0.3 for significant peaks

SELECT 'Monthly Sales - Seasonal Peak Detection' AS example;

WITH sales_array AS (
    SELECT LIST(sales ORDER BY date) AS values
    FROM monthly_sales
),
result AS (
    SELECT ts_detect_peaks(values, 0.3, 8) AS detection  -- prominence > 0.3, min_distance=8 for yearly
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


-- -----------------------------------------------------------------------------
-- Example 7: Heart rate R-peak detection
-- -----------------------------------------------------------------------------
-- Detect heartbeats from ECG-like signal
-- Note: Prominence is normalized (0-1), use 0.01-0.05 for physiological signals

SELECT 'Heart Rate - R-Peak Detection (Heartbeats)' AS example;

WITH heart_array AS (
    SELECT LIST(signal ORDER BY sample_index) AS values
    FROM heart_rate
),
result AS (
    SELECT ts_detect_peaks(values, 0.02, 50) AS detection  -- prominence > 0.02, min_distance 50 samples
    FROM heart_array
)
SELECT
    detection.n_peaks AS heartbeat_count,
    ROUND(detection.n_peaks / 10.0 * 60, 0) AS estimated_bpm,  -- 10 seconds of data
    detection.peaks[1:5] AS first_5_beats,
    ROUND(detection.mean_period, 1) AS avg_samples_between_beats
FROM result;


-- -----------------------------------------------------------------------------
-- Example 8: Compare peak counts with different parameters
-- -----------------------------------------------------------------------------
-- Understand how parameters affect peak detection
-- Note: Prominence is normalized to 0-1 scale

SELECT 'Parameter Sensitivity Analysis' AS example;

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


-- -----------------------------------------------------------------------------
-- Example 9: Extract inter-peak distances for analysis
-- -----------------------------------------------------------------------------
-- Analyze the spacing between consecutive peaks

SELECT 'Inter-Peak Distance Analysis' AS example;

WITH traffic_array AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
),
result AS (
    SELECT ts_detect_peaks(values, 0.05) AS detection  -- normalized prominence
    FROM traffic_array
)
SELECT
    detection.n_peaks AS total_peaks,
    LEN(detection.inter_peak_distances) AS n_intervals,
    ROUND(detection.mean_period, 2) AS mean_period,
    detection.inter_peak_distances[1:5] AS first_5_distances
FROM result;
