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

-- Detect seasonality only on non-intermittent items using ts_detect_periods_by
SELECT 'Detecting seasonality on non-intermittent items:' AS step;

-- Create filtered table for seasonality detection
CREATE OR REPLACE TABLE regular_items_data AS
SELECT m.item_id, m.ds, m.y
FROM m5_sample m
JOIN regular_items r ON m.item_id = r.item_id;

-- Use ts_detect_periods_by for period detection
CREATE OR REPLACE TABLE item_seasonality AS
SELECT
    id AS item_id,
    periods AS detected_periods,
    n_periods AS n_periods_detected,
    primary_period
FROM ts_detect_periods_by('regular_items_data', item_id, ds, y, MAP{});

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

-- Create table with suitable items data for peak detection
CREATE OR REPLACE TABLE suitable_items_data AS
SELECT m.item_id, m.ds, m.y, s.primary_period
FROM m5_sample m
JOIN suitable_items s ON m.item_id = s.item_id;

-- Peak detection using ts_detect_peaks_by
SELECT 'Peak Detection Results:' AS step;

-- Default detection
CREATE OR REPLACE TABLE peak_results_default AS
SELECT * FROM ts_detect_peaks_by('suitable_items_data', item_id, ds, y, MAP{});

-- Significant detection with higher prominence threshold
CREATE OR REPLACE TABLE peak_results_significant AS
SELECT * FROM ts_detect_peaks_by('suitable_items_data', item_id, ds, y, MAP{'min_prominence': '0.2'});

-- Join results with period info
SELECT
    d.id AS item_id,
    s.primary_period AS detected_period,
    d.n_peaks AS all_peaks,
    sig.n_peaks AS significant_peaks,
    ROUND(d.mean_period, 1) AS avg_days_between_peaks
FROM peak_results_default d
JOIN peak_results_significant sig ON d.id = sig.id
JOIN (SELECT DISTINCT item_id, primary_period FROM suitable_items) s ON d.id = s.item_id
ORDER BY sig.n_peaks DESC
LIMIT 10;

-- ============================================================================
-- SECTION 5: Peak Timing Analysis
-- ============================================================================

SELECT
    '=== Section 5: Peak Timing Analysis ===' AS section;

-- Analyze peak timing using ts_analyze_peak_timing_by with weekly period (7.0)
SELECT 'Peak Timing Using Weekly Period:' AS step;

CREATE OR REPLACE TABLE timing_results AS
SELECT * FROM ts_analyze_peak_timing_by('suitable_items_data', item_id, ds, y, 7.0, MAP{});

-- Join with suitable items to get primary period
SELECT
    t.id AS item_id,
    s.primary_period AS period,
    t.n_peaks AS peaks_analyzed,
    ROUND(t.variability_score, 3) AS variability,
    t.is_stable AS stable_pattern,
    CASE
        WHEN t.variability_score < 0.1 THEN 'Very Consistent'
        WHEN t.variability_score < 0.3 THEN 'Consistent'
        WHEN t.variability_score < 0.5 THEN 'Moderate'
        ELSE 'Variable'
    END AS assessment
FROM timing_results t
JOIN (SELECT DISTINCT item_id, primary_period FROM suitable_items) s ON t.id = s.item_id
WHERE t.n_peaks >= 5
ORDER BY t.variability_score
LIMIT 15;

-- Find items with stable peak patterns
SELECT 'Items with Stable Peak Patterns (is_stable = true):' AS step;
SELECT
    t.id AS item_id,
    s.primary_period AS period,
    t.n_peaks AS peaks,
    ROUND(t.variability_score, 4) AS variability
FROM timing_results t
JOIN (SELECT DISTINCT item_id, primary_period FROM suitable_items) s ON t.id = s.item_id
WHERE t.is_stable = true AND t.n_peaks >= 10
ORDER BY t.variability_score
LIMIT 10;

-- ============================================================================
-- SECTION 6: Demand Spike Detection with Dates
-- ============================================================================

SELECT
    '=== Section 6: Demand Spike Detection ===' AS section;

-- Find significant demand spikes for a suitable item using ts_detect_peaks_by
SELECT 'Top Demand Spikes (First Suitable Item):' AS step;

-- Get first suitable item
CREATE OR REPLACE TABLE first_item_data AS
SELECT m.item_id, m.ds, m.y
FROM m5_sample m
WHERE m.item_id = (SELECT item_id FROM suitable_items LIMIT 1);

-- Detect peaks with high prominence
CREATE OR REPLACE TABLE first_item_peaks AS
SELECT * FROM ts_detect_peaks_by('first_item_data', item_id, ds, y, MAP{'min_prominence': '0.3'});

-- Unnest and join with dates
WITH peaks_unnested AS (
    SELECT id AS item_id, UNNEST(peaks) AS peak FROM first_item_peaks
),
numbered_data AS (
    SELECT ROW_NUMBER() OVER (ORDER BY ds) AS row_idx, item_id, ds, y
    FROM first_item_data
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

-- Use existing peak results and timing results from earlier sections
WITH demand_stats AS (
    SELECT
        m.item_id,
        s.primary_period,
        s.demand_type,
        AVG(m.y) AS avg_demand,
        MAX(m.y) AS max_demand
    FROM m5_sample m
    JOIN suitable_items s ON m.item_id = s.item_id
    GROUP BY m.item_id, s.primary_period, s.demand_type
)
SELECT
    d.item_id,
    d.demand_type,
    d.primary_period AS period,
    ROUND(d.avg_demand, 2) AS avg_demand,
    d.max_demand::INT AS max_demand,
    p.n_peaks AS n_peaks,
    ROUND(p.mean_period, 1) AS peak_interval,
    ROUND(t.variability_score, 3) AS timing_var,
    t.is_stable AS stable,
    CASE
        WHEN t.is_stable AND p.n_peaks > 50 THEN 'Stable high-frequency'
        WHEN t.is_stable THEN 'Stable pattern'
        WHEN p.n_peaks > 50 THEN 'Variable high-frequency'
        ELSE 'Variable pattern'
    END AS pattern_type
FROM demand_stats d
JOIN peak_results_default p ON d.item_id = p.id
JOIN timing_results t ON d.item_id = t.id
WHERE t.n_peaks >= 5
ORDER BY t.variability_score
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
    'KEY INSIGHT: Use aid_agg() to filter intermittent items, then ts_detect_periods_by() to find patterns. Only then is peak detection meaningful.' AS takeaway;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS m5_sample;
-- DROP TABLE IF EXISTS item_demand_type;
-- DROP TABLE IF EXISTS regular_items;
-- DROP TABLE IF EXISTS item_seasonality;
-- DROP TABLE IF EXISTS suitable_items;
