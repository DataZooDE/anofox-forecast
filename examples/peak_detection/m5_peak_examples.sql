-- ============================================================================
-- M5 Peak Detection Examples
-- ============================================================================
-- This file demonstrates peak detection and timing analysis on the M5
-- competition dataset. The M5 dataset contains daily sales data for
-- Walmart products.
--
-- IMPORTANT: Peak detection only makes sense for non-intermittent, seasonal
-- time series. This example uses a two-step filtering approach:
--   1. Filter out intermittent demand using aid_agg()
--   2. Detect seasonality on remaining items
--   3. Run peak analysis only on seasonal, non-intermittent items
--
-- Examples included:
--   1. Load M5 data subset
--   2. Intermittency detection (filter using aid_agg)
--   3. Seasonality detection (on non-intermittent items)
--   4. Peak detection on suitable items
--   5. Peak timing analysis using detected period
--   6. Summary with full context
--
-- Note: Uses full M5 dataset (~30k items) to demonstrate realistic filtering.
-- ============================================================================

-- Load the extensions
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Load anofox_statistics for aid_agg (intermittency detection)
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- Load httpfs for remote data access
INSTALL httpfs;
LOAD httpfs;

-- ============================================================================
-- SECTION 1: Load M5 Data
-- ============================================================================

SELECT
    '=== Section 1: Load M5 Data ===' AS section;

-- Load full M5 dataset
CREATE OR REPLACE TABLE m5_sample AS
SELECT
    item_id,
    CAST(timestamp AS TIMESTAMP) AS ds,
    demand AS y
FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
ORDER BY item_id, timestamp;

-- Show sample data info
SELECT
    'M5 Sample Dataset' AS dataset,
    COUNT(DISTINCT item_id) AS n_items,
    COUNT(*) AS n_rows,
    MIN(ds)::DATE AS start_date,
    MAX(ds)::DATE AS end_date
FROM m5_sample;

-- ============================================================================
-- SECTION 2: Intermittency Detection (First Filter)
-- ============================================================================

SELECT
    '=== Section 2: Intermittency Detection ===' AS section;

-- Use aid_agg() to classify demand patterns
-- Intermittent demand is unsuitable for peak detection
SELECT 'Classifying demand patterns with aid_agg():' AS step;

CREATE OR REPLACE TABLE item_demand_type AS
SELECT
    item_id,
    (aid_agg(y)).demand_type AS demand_type,
    (aid_agg(y)).is_intermittent AS is_intermittent,
    (aid_agg(y)).zero_proportion AS zero_proportion,
    (aid_agg(y)).mean AS mean_demand
FROM m5_sample
GROUP BY item_id;

-- Summary of intermittency detection
SELECT 'Intermittency Detection Summary:' AS step;
SELECT
    COUNT(*) AS total_items,
    SUM(CASE WHEN is_intermittent THEN 1 ELSE 0 END) AS intermittent_items,
    SUM(CASE WHEN NOT is_intermittent THEN 1 ELSE 0 END) AS regular_items,
    ROUND(SUM(CASE WHEN NOT is_intermittent THEN 1 ELSE 0 END) * 100.0 / COUNT(*), 1) AS pct_regular
FROM item_demand_type;

-- Show demand type distribution
SELECT 'Demand Type Distribution:' AS step;
SELECT
    demand_type,
    is_intermittent,
    COUNT(*) AS n_items,
    ROUND(AVG(zero_proportion) * 100, 1) AS avg_pct_zeros,
    ROUND(AVG(mean_demand), 2) AS avg_demand
FROM item_demand_type
GROUP BY demand_type, is_intermittent
ORDER BY n_items DESC;

-- Filter to non-intermittent items only
CREATE OR REPLACE TABLE regular_items AS
SELECT item_id, demand_type, zero_proportion, mean_demand
FROM item_demand_type
WHERE NOT is_intermittent;

SELECT 'Non-intermittent items for further analysis:' AS step;
SELECT COUNT(*) AS n_regular_items FROM regular_items;

-- ============================================================================
-- SECTION 3: Seasonality Detection (Second Filter)
-- ============================================================================

SELECT
    '=== Section 3: Seasonality Detection ===' AS section;

-- Detect seasonality only on non-intermittent items
SELECT 'Detecting seasonality on non-intermittent items:' AS step;

CREATE OR REPLACE TABLE item_seasonality AS
WITH item_arrays AS (
    SELECT
        m.item_id,
        LIST(m.y ORDER BY m.ds) AS values
    FROM m5_sample m
    JOIN regular_items r ON m.item_id = r.item_id
    GROUP BY m.item_id
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
SELECT 'Seasonality Detection Summary (non-intermittent items only):' AS step;
SELECT
    COUNT(*) AS regular_items,
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

-- Create final filtered dataset: non-intermittent AND seasonal
CREATE OR REPLACE TABLE suitable_items AS
SELECT s.item_id, s.primary_period, s.detected_periods, r.demand_type, r.mean_demand
FROM item_seasonality s
JOIN regular_items r ON s.item_id = r.item_id
WHERE s.n_periods_detected > 0;

SELECT 'Items suitable for peak analysis (non-intermittent + seasonal):' AS step;
SELECT COUNT(*) AS n_suitable_items FROM suitable_items;

-- ============================================================================
-- SECTION 4: Peak Detection on Suitable Items
-- ============================================================================

SELECT
    '=== Section 4: Peak Detection ===' AS section;

-- Peak detection on filtered items
SELECT 'Peak Detection Results:' AS step;
WITH item_arrays AS (
    SELECT
        m.item_id,
        s.primary_period,
        LIST(m.y ORDER BY m.ds) AS values
    FROM m5_sample m
    JOIN suitable_items s ON m.item_id = s.item_id
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
-- SECTION 5: Peak Timing Analysis
-- ============================================================================

SELECT
    '=== Section 5: Peak Timing Analysis ===' AS section;

-- Analyze peak timing using each item's detected seasonal period
SELECT 'Peak Timing Using Detected Seasonality:' AS step;
WITH item_arrays AS (
    SELECT
        m.item_id,
        s.primary_period,
        LIST(m.y ORDER BY m.ds) AS values
    FROM m5_sample m
    JOIN suitable_items s ON m.item_id = s.item_id
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
WHERE timing.n_peaks >= 5
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
    JOIN suitable_items s ON m.item_id = s.item_id
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
-- SECTION 6: Demand Spike Detection with Dates
-- ============================================================================

SELECT
    '=== Section 6: Demand Spike Detection ===' AS section;

-- Find significant demand spikes for a suitable item
SELECT 'Top Demand Spikes (First Suitable Item):' AS step;
WITH
first_suitable AS (
    SELECT item_id, primary_period FROM suitable_items LIMIT 1
),
numbered_data AS (
    SELECT
        ROW_NUMBER() OVER (ORDER BY ds) AS row_idx,
        m.item_id,
        m.ds,
        m.y
    FROM m5_sample m
    JOIN first_suitable f ON m.item_id = f.item_id
),
item_data AS (
    SELECT
        (SELECT item_id FROM first_suitable) AS item_id,
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
-- SECTION 7: Full Analysis Summary
-- ============================================================================

SELECT
    '=== Section 7: Full Analysis Summary ===' AS section;

-- Show the filtering funnel
SELECT 'Filtering Funnel:' AS step;
SELECT
    (SELECT COUNT(DISTINCT item_id) FROM m5_sample) AS total_items,
    (SELECT COUNT(*) FROM regular_items) AS non_intermittent,
    (SELECT COUNT(*) FROM suitable_items) AS seasonal_and_regular,
    ROUND((SELECT COUNT(*) FROM suitable_items) * 100.0 /
          (SELECT COUNT(DISTINCT item_id) FROM m5_sample), 1) AS pct_suitable;

-- Comprehensive summary for suitable items
SELECT 'Complete Analysis (Suitable Items Only):' AS step;
WITH item_arrays AS (
    SELECT
        m.item_id,
        s.primary_period,
        s.demand_type,
        LIST(m.y ORDER BY m.ds) AS values,
        AVG(m.y) AS avg_demand,
        MAX(m.y) AS max_demand
    FROM m5_sample m
    JOIN suitable_items s ON m.item_id = s.item_id
    GROUP BY m.item_id, s.primary_period, s.demand_type
),
full_analysis AS (
    SELECT
        item_id,
        primary_period,
        demand_type,
        avg_demand,
        max_demand,
        ts_detect_peaks(values, 0.1) AS detection,
        ts_analyze_peak_timing(values, primary_period) AS timing
    FROM item_arrays
)
SELECT
    item_id,
    demand_type,
    primary_period AS period,
    ROUND(avg_demand, 2) AS avg_demand,
    max_demand::INT AS max_demand,
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

-- Compare all item categories
SELECT 'Comparison: All Item Categories:' AS step;
WITH all_items AS (
    SELECT
        m.item_id,
        CASE
            WHEN s.item_id IS NOT NULL THEN 'Suitable (regular + seasonal)'
            WHEN r.item_id IS NOT NULL THEN 'Regular but non-seasonal'
            ELSE 'Intermittent'
        END AS category,
        AVG(m.y) AS avg_demand,
        SUM(CASE WHEN m.y = 0 THEN 1 ELSE 0 END) * 100.0 / COUNT(*) AS pct_zeros
    FROM m5_sample m
    LEFT JOIN suitable_items s ON m.item_id = s.item_id
    LEFT JOIN regular_items r ON m.item_id = r.item_id
    GROUP BY m.item_id,
        CASE
            WHEN s.item_id IS NOT NULL THEN 'Suitable (regular + seasonal)'
            WHEN r.item_id IS NOT NULL THEN 'Regular but non-seasonal'
            ELSE 'Intermittent'
        END
)
SELECT
    category,
    COUNT(*) AS n_items,
    ROUND(AVG(avg_demand), 2) AS avg_daily_demand,
    ROUND(AVG(pct_zeros), 1) AS avg_pct_zeros
FROM all_items
GROUP BY category
ORDER BY
    CASE category
        WHEN 'Suitable (regular + seasonal)' THEN 1
        WHEN 'Regular but non-seasonal' THEN 2
        ELSE 3
    END;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT
    '=== Peak Detection Complete ===' AS section;

-- Key insight
SELECT
    'KEY INSIGHT: Use aid_agg() to filter intermittent items, then ts_detect_seasonality() to find patterns. Only then is peak detection meaningful.' AS takeaway;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS m5_sample;
-- DROP TABLE IF EXISTS item_demand_type;
-- DROP TABLE IF EXISTS regular_items;
-- DROP TABLE IF EXISTS item_seasonality;
-- DROP TABLE IF EXISTS suitable_items;
