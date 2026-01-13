-- ============================================================================
-- M5 Changepoint Detection Examples
-- ============================================================================
-- This file demonstrates changepoint detection on the M5 competition dataset.
-- The M5 dataset contains daily sales data for Walmart products across
-- multiple stores and categories.
--
-- Changepoint detection identifies structural breaks in time series where the
-- statistical properties (mean, variance) change significantly. This is useful
-- for detecting:
--   - Demand regime changes (e.g., product lifecycle phases)
--   - External shocks (e.g., promotions, holidays, supply disruptions)
--   - Trend shifts (e.g., seasonality changes, market dynamics)
--
-- This example uses BOCPD (Bayesian Online Changepoint Detection) which:
--   - Provides probability scores for each point being a changepoint
--   - Works online (streaming) - no need to see future data
--   - Adapts to different series characteristics automatically
--
-- Examples included:
--   1. Load M5 data (full dataset)
--   2. Basic changepoint detection on sample items
--   3. Full dataset analysis with ts_detect_changepoints_by
--   4. Changepoint timing analysis (when do changepoints occur?)
--   5. Category-level changepoint patterns
--   6. Summary statistics and insights
--
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

-- Load full M5 dataset
CREATE OR REPLACE TABLE m5_full AS
SELECT
    item_id,
    CAST(timestamp AS TIMESTAMP) AS ds,
    demand AS y
FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
ORDER BY item_id, timestamp;

-- Show dataset info
SELECT
    'M5 Full Dataset' AS dataset,
    COUNT(DISTINCT item_id) AS n_items,
    COUNT(*) AS n_rows,
    MIN(ds)::DATE AS start_date,
    MAX(ds)::DATE AS end_date,
    COUNT(*) / COUNT(DISTINCT item_id) AS days_per_item
FROM m5_full;

-- Extract metadata from item_id (format: CATEGORY_DEPT_ITEM_STORE_STATE)
-- Example: HOBBIES_1_001_CA_1 -> category=HOBBIES, dept=1, store=CA_1, state=CA
CREATE OR REPLACE TABLE m5_items AS
SELECT DISTINCT
    item_id,
    SPLIT_PART(item_id, '_', 1) AS category,
    SPLIT_PART(item_id, '_', 1) || '_' || SPLIT_PART(item_id, '_', 2) AS dept,
    SPLIT_PART(item_id, '_', 4) || '_' || SPLIT_PART(item_id, '_', 5) AS store,
    SPLIT_PART(item_id, '_', 4) AS state
FROM m5_full;

-- Show category breakdown
SELECT
    'Items by Category' AS breakdown,
    category,
    COUNT(*) AS n_items
FROM m5_items
GROUP BY category
ORDER BY n_items DESC;

-- ============================================================================
-- SECTION 2: Basic Changepoint Detection (Sample Items)
-- ============================================================================

SELECT
    '=== Section 2: Basic Changepoint Detection ===' AS section;

-- Create a small sample for demonstration
CREATE OR REPLACE TABLE m5_sample AS
SELECT * FROM m5_full
WHERE item_id IN (
    SELECT DISTINCT item_id FROM m5_full ORDER BY item_id LIMIT 10
);

-- Run changepoint detection on sample using ts_detect_changepoints_agg
-- hazard_lambda controls expected run length between changepoints
-- Higher values = fewer changepoints expected (more conservative)
SELECT 'Running changepoint detection on 10 sample items...' AS step;

CREATE OR REPLACE TABLE sample_changepoints AS
SELECT
    item_id,
    ts_detect_changepoints_agg(ds, y, MAP{'hazard_lambda': '50'}) AS cp_results
FROM m5_sample
GROUP BY item_id;

-- Show changepoint counts per item
SELECT
    item_id,
    list_count(list_filter(cp_results, x -> x.is_changepoint)) AS n_changepoints,
    list_count(cp_results) AS n_observations
FROM sample_changepoints
ORDER BY n_changepoints DESC;

-- ============================================================================
-- SECTION 3: Full Dataset Changepoint Detection
-- ============================================================================

SELECT
    '=== Section 3: Full Dataset Changepoint Detection ===' AS section;

-- Run changepoint detection on ALL items using ts_detect_changepoints_by
-- This is the most efficient way to process many time series
SELECT 'Running changepoint detection on full M5 dataset (~30k items)...' AS step;
SELECT 'This may take a few minutes...' AS note;

CREATE OR REPLACE TABLE m5_changepoints AS
SELECT
    id AS item_id,
    (changepoints).is_changepoint AS is_changepoint_arr,
    (changepoints).changepoint_probability AS cp_probability_arr,
    (changepoints).changepoint_indices AS cp_indices
FROM ts_detect_changepoints_by(
    'm5_full',
    item_id,
    ds,
    y,
    MAP{'hazard_lambda': '50', 'include_probabilities': 'true'}
);

-- Show sample results
SELECT
    'Sample Results (first 10 items)' AS info,
    item_id,
    list_count(cp_indices) AS n_changepoints
FROM m5_changepoints
LIMIT 10;

-- ============================================================================
-- SECTION 4: Changepoint Timing Analysis
-- ============================================================================

SELECT
    '=== Section 4: Changepoint Timing Analysis ===' AS section;

-- Extract individual changepoint dates for analysis
-- We need to map indices back to dates
CREATE OR REPLACE TABLE cp_dates AS
WITH
-- Get the date list for each item
item_dates AS (
    SELECT
        item_id,
        LIST(ds ORDER BY ds) AS date_list
    FROM m5_full
    GROUP BY item_id
),
-- Unnest changepoint indices
cp_unnested AS (
    SELECT
        item_id,
        UNNEST(cp_indices) AS cp_index
    FROM m5_changepoints
    WHERE list_count(cp_indices) > 0
)
SELECT
    c.item_id,
    c.cp_index,
    d.date_list[c.cp_index + 1] AS cp_date  -- +1 because DuckDB arrays are 1-indexed
FROM cp_unnested c
JOIN item_dates d ON c.item_id = d.item_id;

-- Add metadata
CREATE OR REPLACE TABLE cp_dates_with_meta AS
SELECT
    c.*,
    i.category,
    i.dept,
    i.store,
    i.state,
    EXTRACT(YEAR FROM cp_date) AS cp_year,
    EXTRACT(MONTH FROM cp_date) AS cp_month,
    EXTRACT(DOW FROM cp_date) AS cp_dow,  -- 0=Sunday, 6=Saturday
    EXTRACT(WEEK FROM cp_date) AS cp_week
FROM cp_dates c
JOIN m5_items i ON c.item_id = i.item_id;

-- When do changepoints occur? Monthly distribution
SELECT
    'Changepoints by Month' AS analysis,
    cp_month,
    COUNT(*) AS n_changepoints,
    ROUND(COUNT(*) * 100.0 / SUM(COUNT(*)) OVER (), 1) AS pct
FROM cp_dates_with_meta
GROUP BY cp_month
ORDER BY cp_month;

-- Changepoints by year
SELECT
    'Changepoints by Year' AS analysis,
    cp_year,
    COUNT(*) AS n_changepoints
FROM cp_dates_with_meta
GROUP BY cp_year
ORDER BY cp_year;

-- Changepoints by day of week
SELECT
    'Changepoints by Day of Week' AS analysis,
    CASE cp_dow
        WHEN 0 THEN 'Sunday'
        WHEN 1 THEN 'Monday'
        WHEN 2 THEN 'Tuesday'
        WHEN 3 THEN 'Wednesday'
        WHEN 4 THEN 'Thursday'
        WHEN 5 THEN 'Friday'
        WHEN 6 THEN 'Saturday'
    END AS day_name,
    COUNT(*) AS n_changepoints
FROM cp_dates_with_meta
GROUP BY cp_dow
ORDER BY cp_dow;

-- ============================================================================
-- SECTION 5: Category-Level Changepoint Patterns
-- ============================================================================

SELECT
    '=== Section 5: Category-Level Patterns ===' AS section;

-- Changepoints by category
SELECT
    'Changepoints by Category' AS analysis,
    category,
    COUNT(*) AS n_changepoints,
    COUNT(DISTINCT item_id) AS items_with_cp,
    ROUND(COUNT(*) * 1.0 / COUNT(DISTINCT item_id), 2) AS avg_cp_per_item
FROM cp_dates_with_meta
GROUP BY category
ORDER BY avg_cp_per_item DESC;

-- Changepoints by store
SELECT
    'Changepoints by Store' AS analysis,
    store,
    COUNT(*) AS n_changepoints,
    COUNT(DISTINCT item_id) AS items_with_cp,
    ROUND(COUNT(*) * 1.0 / COUNT(DISTINCT item_id), 2) AS avg_cp_per_item
FROM cp_dates_with_meta
GROUP BY store
ORDER BY avg_cp_per_item DESC
LIMIT 10;

-- Changepoints by state
SELECT
    'Changepoints by State' AS analysis,
    state,
    COUNT(*) AS n_changepoints,
    COUNT(DISTINCT item_id) AS items_with_cp,
    ROUND(COUNT(*) * 1.0 / COUNT(DISTINCT item_id), 2) AS avg_cp_per_item
FROM cp_dates_with_meta
GROUP BY state
ORDER BY avg_cp_per_item DESC;

-- Top 10 most common changepoint dates (potential external events)
SELECT
    'Top 10 Most Common Changepoint Dates' AS analysis,
    cp_date::DATE AS changepoint_date,
    COUNT(*) AS n_items_affected,
    COUNT(DISTINCT category) AS n_categories,
    COUNT(DISTINCT store) AS n_stores
FROM cp_dates_with_meta
GROUP BY cp_date
ORDER BY n_items_affected DESC
LIMIT 10;

-- ============================================================================
-- SECTION 6: Summary Statistics
-- ============================================================================

SELECT
    '=== Section 6: Summary Statistics ===' AS section;

-- Overall changepoint summary
CREATE OR REPLACE TABLE cp_summary AS
SELECT
    COUNT(DISTINCT m.item_id) AS total_items,
    COUNT(DISTINCT CASE WHEN list_count(c.cp_indices) > 0 THEN m.item_id END) AS items_with_changepoints,
    COUNT(DISTINCT CASE WHEN list_count(c.cp_indices) = 0 THEN m.item_id END) AS items_without_changepoints,
    SUM(list_count(c.cp_indices)) AS total_changepoints,
    ROUND(AVG(list_count(c.cp_indices)), 2) AS avg_changepoints_per_item,
    MAX(list_count(c.cp_indices)) AS max_changepoints,
    PERCENTILE_CONT(0.5) WITHIN GROUP (ORDER BY list_count(c.cp_indices)) AS median_changepoints
FROM (SELECT DISTINCT item_id FROM m5_full) m
LEFT JOIN m5_changepoints c ON m.item_id = c.item_id;

-- Display summary
SELECT
    '=== CHANGEPOINT DETECTION SUMMARY ===' AS report_title;

SELECT
    total_items,
    items_with_changepoints,
    items_without_changepoints,
    ROUND(items_with_changepoints * 100.0 / total_items, 1) AS pct_with_changepoints,
    total_changepoints,
    avg_changepoints_per_item,
    max_changepoints,
    median_changepoints
FROM cp_summary;

-- Category breakdown summary
SELECT
    '=== CHANGEPOINTS BY CATEGORY ===' AS report_section;

SELECT
    i.category,
    COUNT(DISTINCT i.item_id) AS total_items,
    COUNT(DISTINCT CASE WHEN list_count(c.cp_indices) > 0 THEN i.item_id END) AS items_with_cp,
    ROUND(COUNT(DISTINCT CASE WHEN list_count(c.cp_indices) > 0 THEN i.item_id END) * 100.0 /
          COUNT(DISTINCT i.item_id), 1) AS pct_with_cp,
    COALESCE(SUM(list_count(c.cp_indices)), 0) AS total_cps,
    ROUND(COALESCE(AVG(list_count(c.cp_indices)), 0), 2) AS avg_cps_per_item
FROM m5_items i
LEFT JOIN m5_changepoints c ON i.item_id = c.item_id
GROUP BY i.category
ORDER BY avg_cps_per_item DESC;

-- Temporal pattern summary
SELECT
    '=== TEMPORAL PATTERNS ===' AS report_section;

SELECT
    'Peak changepoint months (top 3)' AS metric,
    STRING_AGG(month_name, ', ') AS value
FROM (
    SELECT
        CASE cp_month
            WHEN 1 THEN 'Jan' WHEN 2 THEN 'Feb' WHEN 3 THEN 'Mar'
            WHEN 4 THEN 'Apr' WHEN 5 THEN 'May' WHEN 6 THEN 'Jun'
            WHEN 7 THEN 'Jul' WHEN 8 THEN 'Aug' WHEN 9 THEN 'Sep'
            WHEN 10 THEN 'Oct' WHEN 11 THEN 'Nov' WHEN 12 THEN 'Dec'
        END AS month_name,
        COUNT(*) AS n
    FROM cp_dates_with_meta
    GROUP BY cp_month
    ORDER BY n DESC
    LIMIT 3
);

-- Items with most changepoints (potentially high volatility)
SELECT
    '=== TOP 10 MOST VOLATILE ITEMS ===' AS report_section;

SELECT
    c.item_id,
    i.category,
    i.store,
    list_count(c.cp_indices) AS n_changepoints
FROM m5_changepoints c
JOIN m5_items i ON c.item_id = i.item_id
ORDER BY list_count(c.cp_indices) DESC
LIMIT 10;

-- Stable items (no changepoints detected)
SELECT
    '=== STABLE ITEMS SAMPLE (no changepoints) ===' AS report_section;

SELECT
    c.item_id,
    i.category,
    i.store
FROM m5_changepoints c
JOIN m5_items i ON c.item_id = i.item_id
WHERE list_count(c.cp_indices) = 0
LIMIT 10;

-- ============================================================================
-- FINAL SUMMARY TABLE
-- ============================================================================

SELECT
    '=== FINAL SUMMARY ===' AS report_section;

-- Create a comprehensive summary table
CREATE OR REPLACE TABLE changepoint_analysis_summary AS
WITH
item_stats AS (
    SELECT
        c.item_id,
        i.category,
        i.dept,
        i.store,
        i.state,
        list_count(c.cp_indices) AS n_changepoints,
        CASE
            WHEN list_count(c.cp_indices) = 0 THEN 'Stable'
            WHEN list_count(c.cp_indices) <= 2 THEN 'Low volatility'
            WHEN list_count(c.cp_indices) <= 5 THEN 'Medium volatility'
            ELSE 'High volatility'
        END AS volatility_class
    FROM m5_changepoints c
    JOIN m5_items i ON c.item_id = i.item_id
),
category_stats AS (
    SELECT
        category,
        COUNT(*) AS n_items,
        SUM(n_changepoints) AS total_cps,
        ROUND(AVG(n_changepoints), 2) AS avg_cps,
        SUM(CASE WHEN volatility_class = 'Stable' THEN 1 ELSE 0 END) AS stable_items,
        SUM(CASE WHEN volatility_class = 'High volatility' THEN 1 ELSE 0 END) AS volatile_items
    FROM item_stats
    GROUP BY category
)
SELECT * FROM category_stats
ORDER BY avg_cps DESC;

-- Display final summary
SELECT * FROM changepoint_analysis_summary;

-- ============================================================================
-- CLEANUP (optional)
-- ============================================================================

SELECT
    '=== Changepoint Analysis Complete ===' AS section;

-- Uncomment to clean up temporary tables
-- DROP TABLE IF EXISTS m5_full;
-- DROP TABLE IF EXISTS m5_sample;
-- DROP TABLE IF EXISTS m5_items;
-- DROP TABLE IF EXISTS m5_changepoints;
-- DROP TABLE IF EXISTS sample_changepoints;
-- DROP TABLE IF EXISTS cp_dates;
-- DROP TABLE IF EXISTS cp_dates_with_meta;
-- DROP TABLE IF EXISTS cp_summary;
-- DROP TABLE IF EXISTS changepoint_analysis_summary;
