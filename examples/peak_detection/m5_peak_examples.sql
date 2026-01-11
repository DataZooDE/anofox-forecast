-- ============================================================================
-- M5 Peak Detection Examples
-- ============================================================================
-- This file demonstrates peak detection and timing analysis on the M5
-- competition dataset. The M5 dataset contains daily sales data for
-- Walmart products.
--
-- IMPORTANT: Peak detection only makes sense for seasonal time series.
-- This example first detects seasonality, then filters to items with
-- detected patterns before running peak analysis.
--
-- Examples included:
--   1. Load M5 data subset
--   2. Seasonality detection (filter for meaningful analysis)
--   3. Peak detection on seasonal items
--   4. Peak timing analysis using detected period
--   5. Multi-item comparison (seasonal items only)
--   6. Summary with seasonality context
--
-- Note: We use a subset of the data (50 items) to find enough seasonal series.
-- ============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Load httpfs for remote data access
INSTALL httpfs;
LOAD httpfs;

-- ============================================================================
-- SECTION 1: Load M5 Data
-- ============================================================================

SELECT
    '=== Section 1: Load M5 Data ===' AS section;

-- Load M5 dataset subset (50 items for variety)
CREATE OR REPLACE TABLE m5_sample AS
SELECT
    item_id,
    CAST(timestamp AS TIMESTAMP) AS ds,
    demand AS y
FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
WHERE item_id IN (
    SELECT DISTINCT item_id
    FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
    ORDER BY item_id
    LIMIT 50
)
ORDER BY item_id, timestamp;

-- Show sample data info
SELECT
    'M5 Sample Dataset' AS dataset,
    COUNT(DISTINCT item_id) AS n_items,
    COUNT(*) AS n_rows,
    MIN(ds)::DATE AS start_date,
    MAX(ds)::DATE AS end_date
FROM m5_sample;

-- Show data characteristics (intermittent demand warning)
SELECT 'Data Characteristics (why seasonality detection matters):' AS step;
WITH item_stats AS (
    SELECT
        item_id,
        COUNT(*) AS n_days,
        SUM(CASE WHEN y = 0 THEN 1 ELSE 0 END) AS zero_days,
        AVG(y) AS avg_demand
    FROM m5_sample
    GROUP BY item_id
)
SELECT
    ROUND(AVG(zero_days * 100.0 / n_days), 1) AS avg_pct_zero_days,
    ROUND(AVG(avg_demand), 2) AS avg_daily_demand,
    'Intermittent demand - peak detection only meaningful for seasonal items' AS note
FROM item_stats;

-- ============================================================================
-- SECTION 2: Seasonality Detection (Critical First Step)
-- ============================================================================

SELECT
    '=== Section 2: Seasonality Detection ===' AS section;

-- Detect seasonality for all items
-- This determines which items have patterns suitable for peak analysis
SELECT 'Detecting seasonality for all items:' AS step;

CREATE OR REPLACE TABLE item_seasonality AS
WITH item_arrays AS (
    SELECT
        item_id,
        LIST(y ORDER BY ds) AS values
    FROM m5_sample
    GROUP BY item_id
)
SELECT
    item_id,
    ts_detect_seasonality(values) AS detected_periods,
    LEN(ts_detect_seasonality(values)) AS n_periods_detected,
    CASE
        WHEN LEN(ts_detect_seasonality(values)) > 0 THEN ts_detect_seasonality(values)[1]
        ELSE NULL
    END AS primary_period
FROM item_arrays;

-- Summary of seasonality detection
SELECT 'Seasonality Detection Summary:' AS step;
SELECT
    COUNT(*) AS total_items,
    SUM(CASE WHEN n_periods_detected > 0 THEN 1 ELSE 0 END) AS seasonal_items,
    SUM(CASE WHEN n_periods_detected = 0 THEN 1 ELSE 0 END) AS non_seasonal_items,
    ROUND(SUM(CASE WHEN n_periods_detected > 0 THEN 1 ELSE 0 END) * 100.0 / COUNT(*), 1) AS pct_seasonal
FROM item_seasonality;

-- Show detected periods distribution
SELECT 'Detected Primary Periods:' AS step;
SELECT
    primary_period,
    COUNT(*) AS n_items,
    CASE primary_period
        WHEN 7 THEN 'Weekly'
        WHEN 14 THEN 'Bi-weekly'
        WHEN 30 THEN 'Monthly'
        WHEN 365 THEN 'Yearly'
        ELSE 'Other'
    END AS period_type
FROM item_seasonality
WHERE primary_period IS NOT NULL
GROUP BY primary_period
ORDER BY n_items DESC
LIMIT 10;

-- ============================================================================
-- SECTION 3: Peak Detection on Seasonal Items Only
-- ============================================================================

SELECT
    '=== Section 3: Peak Detection (Seasonal Items) ===' AS section;

-- Create filtered dataset of seasonal items only
CREATE OR REPLACE TABLE seasonal_items AS
SELECT s.item_id, s.primary_period, s.detected_periods
FROM item_seasonality s
WHERE s.n_periods_detected > 0;

SELECT 'Seasonal items selected for peak analysis:' AS step;
SELECT COUNT(*) AS n_seasonal_items FROM seasonal_items;

-- Peak detection on seasonal items
SELECT 'Peak Detection Results:' AS step;
WITH item_arrays AS (
    SELECT
        m.item_id,
        s.primary_period,
        LIST(m.y ORDER BY m.ds) AS values
    FROM m5_sample m
    JOIN seasonal_items s ON m.item_id = s.item_id
    GROUP BY m.item_id, s.primary_period
),
peak_results AS (
    SELECT
        item_id,
        primary_period,
        ts_detect_peaks(values) AS detection,
        ts_detect_peaks(values, 0.2) AS significant_detection
    FROM item_arrays
)
SELECT
    item_id,
    primary_period AS detected_period,
    detection.n_peaks AS all_peaks,
    significant_detection.n_peaks AS significant_peaks,
    ROUND(detection.mean_period, 1) AS avg_days_between_peaks
FROM peak_results
ORDER BY significant_detection.n_peaks DESC
LIMIT 10;

-- ============================================================================
-- SECTION 4: Peak Timing Analysis Using Detected Period
-- ============================================================================

SELECT
    '=== Section 4: Peak Timing Analysis ===' AS section;

-- Analyze peak timing using each item's detected seasonal period
SELECT 'Peak Timing Using Detected Seasonality:' AS step;
WITH item_arrays AS (
    SELECT
        m.item_id,
        s.primary_period,
        LIST(m.y ORDER BY m.ds) AS values
    FROM m5_sample m
    JOIN seasonal_items s ON m.item_id = s.item_id
    GROUP BY m.item_id, s.primary_period
),
timing_results AS (
    SELECT
        item_id,
        primary_period,
        ts_analyze_peak_timing(values, primary_period) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    primary_period AS period,
    timing.n_peaks AS peaks_analyzed,
    ROUND(timing.mean_timing * primary_period, 1) AS mean_peak_position,
    ROUND(timing.variability_score, 3) AS variability,
    timing.is_stable AS stable_pattern,
    CASE
        WHEN timing.variability_score < 0.1 THEN 'Very Consistent'
        WHEN timing.variability_score < 0.3 THEN 'Consistent'
        WHEN timing.variability_score < 0.5 THEN 'Moderate'
        ELSE 'Variable'
    END AS assessment
FROM timing_results
WHERE timing.n_peaks >= 5  -- Need enough peaks for reliable timing analysis
ORDER BY timing.variability_score
LIMIT 15;

-- Find items with stable peak patterns
SELECT 'Items with Stable Peak Patterns (is_stable = true):' AS step;
WITH item_arrays AS (
    SELECT
        m.item_id,
        s.primary_period,
        LIST(m.y ORDER BY m.ds) AS values
    FROM m5_sample m
    JOIN seasonal_items s ON m.item_id = s.item_id
    GROUP BY m.item_id, s.primary_period
),
timing_results AS (
    SELECT
        item_id,
        primary_period,
        ts_analyze_peak_timing(values, primary_period) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    primary_period AS period,
    timing.n_peaks AS peaks,
    ROUND(timing.variability_score, 4) AS variability,
    ROUND(timing.timing_trend, 4) AS trend
FROM timing_results
WHERE timing.is_stable = true AND timing.n_peaks >= 10
ORDER BY timing.variability_score
LIMIT 10;

-- ============================================================================
-- SECTION 5: Demand Spike Detection with Dates
-- ============================================================================

SELECT
    '=== Section 5: Demand Spike Detection ===' AS section;

-- Find significant demand spikes for a seasonal item
SELECT 'Top Demand Spikes (First Seasonal Item):' AS step;
WITH
first_seasonal AS (
    SELECT item_id, primary_period FROM seasonal_items LIMIT 1
),
numbered_data AS (
    SELECT
        ROW_NUMBER() OVER (ORDER BY ds) AS row_idx,
        m.item_id,
        m.ds,
        m.y
    FROM m5_sample m
    JOIN first_seasonal f ON m.item_id = f.item_id
),
item_data AS (
    SELECT
        (SELECT item_id FROM first_seasonal) AS item_id,
        LIST(y ORDER BY ds) AS values
    FROM numbered_data
),
result AS (
    SELECT item_id, ts_detect_peaks(values, 0.3) AS detection
    FROM item_data
),
peaks_unnested AS (
    SELECT item_id, UNNEST(detection.peaks) AS peak
    FROM result
)
SELECT
    p.item_id,
    p.peak.index AS peak_index,
    d.ds::DATE AS peak_date,
    EXTRACT(DOW FROM d.ds) AS day_of_week,
    EXTRACT(MONTH FROM d.ds) AS month,
    d.y AS demand_value,
    ROUND(p.peak.prominence, 4) AS prominence
FROM peaks_unnested p
JOIN numbered_data d ON d.row_idx = p.peak.index + 1
ORDER BY p.peak.prominence DESC
LIMIT 10;

-- ============================================================================
-- SECTION 6: Summary with Seasonality Context
-- ============================================================================

SELECT
    '=== Section 6: Full Analysis Summary ===' AS section;

-- Comprehensive summary: seasonality + peaks + timing
SELECT 'Complete Analysis (Seasonal Items Only):' AS step;
WITH item_arrays AS (
    SELECT
        m.item_id,
        s.primary_period,
        s.detected_periods,
        LIST(m.y ORDER BY m.ds) AS values,
        AVG(m.y) AS avg_demand,
        MAX(m.y) AS max_demand,
        SUM(CASE WHEN m.y = 0 THEN 1 ELSE 0 END) * 100.0 / COUNT(*) AS pct_zeros
    FROM m5_sample m
    JOIN seasonal_items s ON m.item_id = s.item_id
    GROUP BY m.item_id, s.primary_period, s.detected_periods
),
full_analysis AS (
    SELECT
        item_id,
        primary_period,
        detected_periods,
        avg_demand,
        max_demand,
        pct_zeros,
        ts_detect_peaks(values, 0.1) AS detection,
        ts_analyze_peak_timing(values, primary_period) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    primary_period AS period,
    ROUND(avg_demand, 2) AS avg_demand,
    max_demand::INT AS max_demand,
    ROUND(pct_zeros, 1) AS pct_zeros,
    detection.n_peaks AS n_peaks,
    ROUND(detection.mean_period, 1) AS peak_interval,
    ROUND(timing.variability_score, 3) AS timing_var,
    timing.is_stable AS stable,
    CASE
        WHEN timing.is_stable AND detection.n_peaks > 50 THEN 'Stable high-frequency'
        WHEN timing.is_stable THEN 'Stable pattern'
        WHEN detection.n_peaks > 50 THEN 'Variable high-frequency'
        ELSE 'Variable pattern'
    END AS pattern_type
FROM full_analysis
WHERE timing.n_peaks >= 5
ORDER BY timing.variability_score
LIMIT 15;

-- Compare seasonal vs non-seasonal items
SELECT 'Comparison: Seasonal vs Non-Seasonal Items:' AS step;
WITH all_items AS (
    SELECT
        m.item_id,
        CASE WHEN s.item_id IS NOT NULL THEN 'Seasonal' ELSE 'Non-Seasonal' END AS category,
        AVG(m.y) AS avg_demand,
        SUM(CASE WHEN m.y = 0 THEN 1 ELSE 0 END) * 100.0 / COUNT(*) AS pct_zeros
    FROM m5_sample m
    LEFT JOIN seasonal_items s ON m.item_id = s.item_id
    GROUP BY m.item_id, CASE WHEN s.item_id IS NOT NULL THEN 'Seasonal' ELSE 'Non-Seasonal' END
)
SELECT
    category,
    COUNT(*) AS n_items,
    ROUND(AVG(avg_demand), 2) AS avg_daily_demand,
    ROUND(AVG(pct_zeros), 1) AS avg_pct_zeros,
    'Peak analysis meaningful only for Seasonal items' AS note
FROM all_items
GROUP BY category
ORDER BY category;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT
    '=== Peak Detection Complete ===' AS section;

-- Key insight
SELECT
    'KEY INSIGHT: Always detect seasonality first. Peak detection on intermittent/non-seasonal data produces meaningless results.' AS takeaway;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS m5_sample;
-- DROP TABLE IF EXISTS item_seasonality;
-- DROP TABLE IF EXISTS seasonal_items;
