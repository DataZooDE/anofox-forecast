-- ============================================================================
-- M5 Peak Detection Examples
-- ============================================================================
-- This file demonstrates peak detection and timing analysis on the M5
-- competition dataset. The M5 dataset contains daily sales data for
-- Walmart products.
--
-- Examples included:
--   1. Load M5 data subset
--   2. Basic peak detection on sales data
--   3. Weekly peak timing analysis
--   4. Multi-item peak comparison
--   5. Demand spike detection
--   6. Peak consistency across items
--
-- Note: We use a subset of the data (10 items) to keep execution time reasonable.
-- ============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Load httpfs for remote data access
INSTALL httpfs;
LOAD httpfs;

-- ============================================================================
-- SECTION 1: Load M5 Data (subset for faster execution)
-- ============================================================================

SELECT
    '=== Section 1: Load M5 Data ===' AS section;

-- Load full M5 dataset
CREATE OR REPLACE TABLE m5_full AS
SELECT
    item_id,
    CAST(timestamp AS TIMESTAMP) AS ds,
    demand AS y
FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
ORDER BY item_id, timestamp;

-- Create a subset with 10 items for faster processing
CREATE OR REPLACE TABLE m5_sample AS
SELECT * FROM m5_full
WHERE item_id IN (
    SELECT DISTINCT item_id FROM m5_full ORDER BY item_id LIMIT 10
);

-- Show sample data info
SELECT
    'M5 Sample Dataset' AS dataset,
    COUNT(DISTINCT item_id) AS n_items,
    COUNT(*) AS n_rows,
    MIN(ds)::DATE AS start_date,
    MAX(ds)::DATE AS end_date
FROM m5_sample;

-- ============================================================================
-- SECTION 2: Basic Peak Detection on Sales Data
-- ============================================================================

SELECT
    '=== Section 2: Basic Peak Detection ===' AS section;

-- Detect demand peaks for a single item
-- First, let's see the data shape
SELECT 'Sample Item Data Shape:' AS step;
SELECT
    item_id,
    COUNT(*) AS n_days,
    ROUND(AVG(y), 2) AS avg_demand,
    MAX(y) AS max_demand
FROM m5_sample
WHERE item_id = (SELECT item_id FROM m5_sample LIMIT 1)
GROUP BY item_id;

-- Detect all demand peaks for first item
SELECT 'All Peaks (First Item):' AS step;
WITH item_data AS (
    SELECT LIST(y ORDER BY ds) AS values
    FROM m5_sample
    WHERE item_id = (SELECT item_id FROM m5_sample LIMIT 1)
),
result AS (
    SELECT ts_detect_peaks(values) AS detection
    FROM item_data
)
SELECT
    detection.n_peaks AS total_peaks,
    ROUND(detection.mean_period, 2) AS avg_days_between_peaks,
    detection.peaks[1:5] AS first_5_peaks
FROM result;

-- Detect significant demand spikes (high prominence)
SELECT 'Significant Demand Spikes (Prominence > 0.3):' AS step;
WITH item_data AS (
    SELECT LIST(y ORDER BY ds) AS values
    FROM m5_sample
    WHERE item_id = (SELECT item_id FROM m5_sample LIMIT 1)
),
result AS (
    SELECT ts_detect_peaks(values, 0.3) AS detection
    FROM item_data
)
SELECT
    detection.n_peaks AS significant_spikes,
    detection.peaks AS spikes,
    detection.inter_peak_distances AS days_between_spikes
FROM result;

-- ============================================================================
-- SECTION 3: Weekly Peak Timing Analysis
-- ============================================================================

SELECT
    '=== Section 3: Weekly Peak Timing Analysis ===' AS section;

-- Analyze if demand peaks occur on consistent days of the week
-- Period = 7 for weekly cycle
SELECT 'Weekly Peak Timing (First Item):' AS step;
WITH item_data AS (
    SELECT LIST(y ORDER BY ds) AS values
    FROM m5_sample
    WHERE item_id = (SELECT item_id FROM m5_sample LIMIT 1)
),
result AS (
    SELECT ts_analyze_peak_timing(values, 7) AS timing
    FROM item_data
)
SELECT
    timing.n_peaks AS peaks_detected,
    ROUND(timing.mean_timing * 7, 1) AS mean_peak_day_of_week,
    ROUND(timing.std_timing * 7, 2) AS day_std,
    ROUND(timing.variability_score, 3) AS variability,
    timing.is_stable AS consistent_weekly_pattern,
    CASE
        WHEN timing.variability_score < 0.1 THEN 'Very Consistent'
        WHEN timing.variability_score < 0.3 THEN 'Consistent'
        WHEN timing.variability_score < 0.5 THEN 'Moderate Variability'
        ELSE 'High Variability'
    END AS assessment
FROM result;

-- Map day of week to name
SELECT 'Peak Day Interpretation:' AS step;
WITH item_data AS (
    SELECT LIST(y ORDER BY ds) AS values
    FROM m5_sample
    WHERE item_id = (SELECT item_id FROM m5_sample LIMIT 1)
),
result AS (
    SELECT ts_analyze_peak_timing(values, 7) AS timing
    FROM item_data
)
SELECT
    ROUND(timing.mean_timing * 7, 0)::INT AS peak_day_index,
    CASE ROUND(timing.mean_timing * 7, 0)::INT
        WHEN 0 THEN 'Sunday'
        WHEN 1 THEN 'Monday'
        WHEN 2 THEN 'Tuesday'
        WHEN 3 THEN 'Wednesday'
        WHEN 4 THEN 'Thursday'
        WHEN 5 THEN 'Friday'
        WHEN 6 THEN 'Saturday'
        ELSE 'Unknown'
    END AS typical_peak_day
FROM result;

-- ============================================================================
-- SECTION 4: Multi-Item Peak Comparison
-- ============================================================================

SELECT
    '=== Section 4: Multi-Item Peak Comparison ===' AS section;

-- Compare peak characteristics across all items
SELECT 'Peak Counts by Item:' AS step;
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values
    FROM m5_sample
    GROUP BY item_id
)
SELECT
    item_id,
    (ts_detect_peaks(values)).n_peaks AS all_peaks,
    (ts_detect_peaks(values, 0.1)).n_peaks AS minor_peaks,
    (ts_detect_peaks(values, 0.3)).n_peaks AS major_peaks,
    (ts_detect_peaks(values, 0.5)).n_peaks AS extreme_peaks
FROM item_arrays
ORDER BY item_id;

-- Average period between peaks
SELECT 'Peak Frequency by Item:' AS step;
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values
    FROM m5_sample
    GROUP BY item_id
),
results AS (
    SELECT
        item_id,
        ts_detect_peaks(values, 0.1) AS detection
    FROM item_arrays
)
SELECT
    item_id,
    detection.n_peaks AS n_peaks,
    ROUND(detection.mean_period, 1) AS avg_days_between_peaks
FROM results
WHERE detection.n_peaks > 1
ORDER BY detection.mean_period;

-- ============================================================================
-- SECTION 5: Demand Spike Detection
-- ============================================================================

SELECT
    '=== Section 5: Demand Spike Detection ===' AS section;

-- Find the most significant demand spikes with timestamps
SELECT 'Top Demand Spikes with Dates (First Item):' AS step;
WITH
item_id_val AS (
    SELECT item_id FROM m5_sample LIMIT 1
),
numbered_data AS (
    SELECT
        ROW_NUMBER() OVER (ORDER BY ds) AS row_idx,
        ds,
        y
    FROM m5_sample
    WHERE item_id = (SELECT item_id FROM item_id_val)
),
item_data AS (
    SELECT LIST(y ORDER BY ds) AS values
    FROM numbered_data
),
result AS (
    SELECT ts_detect_peaks(values, 0.2, 7) AS detection
    FROM item_data
),
peaks_unnested AS (
    SELECT UNNEST(detection.peaks) AS peak
    FROM result
)
SELECT
    p.peak.index AS peak_index,
    d.ds::DATE AS peak_date,
    EXTRACT(DOW FROM d.ds) AS day_of_week,
    d.y AS demand_value,
    ROUND(p.peak.prominence, 4) AS prominence
FROM peaks_unnested p
JOIN numbered_data d ON d.row_idx = p.peak.index + 1
ORDER BY p.peak.prominence DESC
LIMIT 10;

-- Count extreme spikes per item
SELECT 'Extreme Spike Counts by Item:' AS step;
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values
    FROM m5_sample
    GROUP BY item_id
),
results AS (
    SELECT
        item_id,
        ts_detect_peaks(values, 0.5) AS detection
    FROM item_arrays
)
SELECT
    item_id,
    detection.n_peaks AS extreme_spikes,
    CASE
        WHEN detection.n_peaks = 0 THEN 'No extreme spikes'
        WHEN detection.n_peaks < 5 THEN 'Few extreme spikes'
        WHEN detection.n_peaks < 20 THEN 'Moderate extreme spikes'
        ELSE 'Many extreme spikes'
    END AS spike_assessment
FROM results
ORDER BY detection.n_peaks DESC;

-- ============================================================================
-- SECTION 6: Peak Consistency Across Items
-- ============================================================================

SELECT
    '=== Section 6: Peak Timing Consistency ===' AS section;

-- Analyze weekly peak consistency for all items
SELECT 'Weekly Peak Consistency by Item:' AS step;
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values
    FROM m5_sample
    GROUP BY item_id
),
results AS (
    SELECT
        item_id,
        ts_analyze_peak_timing(values, 7) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    timing.n_peaks AS peaks_detected,
    ROUND(timing.mean_timing * 7, 1) AS mean_peak_day,
    ROUND(timing.variability_score, 3) AS variability,
    timing.is_stable AS stable_pattern,
    CASE
        WHEN timing.is_stable THEN 'Predictable'
        ELSE 'Variable'
    END AS pattern_type
FROM results
ORDER BY timing.variability_score;

-- Find items with most consistent peak patterns
SELECT 'Most Consistent Items (Lowest Variability):' AS step;
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values
    FROM m5_sample
    GROUP BY item_id
),
results AS (
    SELECT
        item_id,
        ts_analyze_peak_timing(values, 7) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    timing.n_peaks AS peaks,
    ROUND(timing.variability_score, 4) AS variability,
    timing.is_stable AS stable,
    CASE ROUND(timing.mean_timing * 7, 0)::INT
        WHEN 0 THEN 'Sunday'
        WHEN 1 THEN 'Monday'
        WHEN 2 THEN 'Tuesday'
        WHEN 3 THEN 'Wednesday'
        WHEN 4 THEN 'Thursday'
        WHEN 5 THEN 'Friday'
        WHEN 6 THEN 'Saturday'
        ELSE 'Unknown'
    END AS typical_peak_day
FROM results
WHERE timing.n_peaks >= 10  -- Need enough peaks for reliable analysis
ORDER BY timing.variability_score
LIMIT 5;

-- Check for timing drift (peaks shifting over time)
SELECT 'Peak Timing Drift Analysis:' AS step;
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values
    FROM m5_sample
    GROUP BY item_id
),
results AS (
    SELECT
        item_id,
        ts_analyze_peak_timing(values, 7) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    ROUND(timing.timing_trend, 4) AS timing_trend,
    ROUND(timing.timing_trend * 7, 2) AS days_shift_per_week,
    CASE
        WHEN timing.timing_trend > 0.02 THEN 'Peaks drifting later'
        WHEN timing.timing_trend < -0.02 THEN 'Peaks drifting earlier'
        ELSE 'Stable timing'
    END AS drift_assessment
FROM results
WHERE timing.n_peaks >= 10
ORDER BY ABS(timing.timing_trend) DESC;

-- ============================================================================
-- SECTION 7: Combined Analysis Summary
-- ============================================================================

SELECT
    '=== Section 7: Summary Statistics ===' AS section;

-- Summary table for all items
SELECT 'Full Summary by Item:' AS step;
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values,
        AVG(y) AS avg_demand,
        MAX(y) AS max_demand
    FROM m5_sample
    GROUP BY item_id
),
peak_analysis AS (
    SELECT
        item_id,
        avg_demand,
        max_demand,
        ts_detect_peaks(values, 0.1) AS detection,
        ts_analyze_peak_timing(values, 7) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    ROUND(avg_demand, 1) AS avg_demand,
    max_demand,
    detection.n_peaks AS n_peaks,
    ROUND(detection.mean_period, 1) AS avg_days_between_peaks,
    ROUND(timing.variability_score, 3) AS timing_variability,
    timing.is_stable AS predictable,
    CASE
        WHEN timing.is_stable AND detection.n_peaks > 50 THEN 'High-frequency predictable'
        WHEN timing.is_stable THEN 'Predictable pattern'
        WHEN detection.n_peaks > 50 THEN 'High-frequency variable'
        ELSE 'Low-frequency variable'
    END AS demand_pattern
FROM peak_analysis
ORDER BY item_id;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT
    '=== Peak Detection Complete ===' AS section;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS m5_full;
-- DROP TABLE IF EXISTS m5_sample;
