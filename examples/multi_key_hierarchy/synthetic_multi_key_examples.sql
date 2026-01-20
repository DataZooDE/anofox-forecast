-- =============================================================================
-- Multi-Key Hierarchy Examples
-- =============================================================================
-- This file demonstrates the multi-key unique_id functions for handling
-- hierarchical time series data (Issue #78).
--
-- Run with: ./build/release/duckdb < examples/multi_key_hierarchy/synthetic_multi_key_examples.sql
-- =============================================================================

.echo on

-- =============================================================================
-- Setup: Create sample sales data with hierarchical structure
-- =============================================================================
CREATE OR REPLACE TABLE sales_data AS
SELECT * FROM (VALUES
    -- EU Region
    ('EU', 'STORE001', 'SKU42', DATE '2024-01-01', 100),
    ('EU', 'STORE001', 'SKU42', DATE '2024-01-02', 110),
    ('EU', 'STORE001', 'SKU42', DATE '2024-01-03', 105),
    ('EU', 'STORE001', 'SKU43', DATE '2024-01-01', 50),
    ('EU', 'STORE001', 'SKU43', DATE '2024-01-02', 55),
    ('EU', 'STORE001', 'SKU43', DATE '2024-01-03', 52),
    ('EU', 'STORE002', 'SKU44', DATE '2024-01-01', 75),
    ('EU', 'STORE002', 'SKU44', DATE '2024-01-02', 80),
    ('EU', 'STORE002', 'SKU44', DATE '2024-01-03', 78),
    -- US Region
    ('US', 'STORE003', 'SKU45', DATE '2024-01-01', 200),
    ('US', 'STORE003', 'SKU45', DATE '2024-01-02', 210),
    ('US', 'STORE003', 'SKU45', DATE '2024-01-03', 205),
    ('US', 'STORE004', 'SKU46', DATE '2024-01-01', 150),
    ('US', 'STORE004', 'SKU46', DATE '2024-01-02', 155),
    ('US', 'STORE004', 'SKU46', DATE '2024-01-03', 152)
) AS t(region_id, store_id, item_id, sale_date, quantity);

SELECT '=== Sample Data ===' AS section;
SELECT * FROM sales_data ORDER BY region_id, store_id, item_id, sale_date LIMIT 10;

-- =============================================================================
-- Section 1: Validate Separator
-- =============================================================================
-- Before combining keys, verify the separator doesn't exist in any ID values.

SELECT '=== Section 1: Validate Separator ===' AS section;

-- Test with default separator (|)
SELECT '--- Check default separator | ---' AS subsection;
SELECT * FROM ts_validate_separator('sales_data', region_id, store_id, item_id);

-- Test with alternative separator
SELECT '--- Check alternative separator - ---' AS subsection;
SELECT * FROM ts_validate_separator('sales_data', region_id, store_id, item_id, separator := '-');

-- Example with conflicting data
CREATE OR REPLACE TABLE conflicting_data AS
SELECT * FROM (VALUES
    ('EU', 'STORE|001', 'SKU42', DATE '2024-01-01', 100)
) AS t(region_id, store_id, item_id, sale_date, quantity);

SELECT '--- Conflict detection example ---' AS subsection;
SELECT * FROM ts_validate_separator('conflicting_data', region_id, store_id, item_id);

-- =============================================================================
-- Section 2: Combine Keys (No Aggregation)
-- =============================================================================
-- Simple key combination for when you just need a single unique_id column.

SELECT '=== Section 2: Combine Keys ===' AS section;

-- Basic combination with 3 columns
SELECT '--- Basic combination ---' AS subsection;
SELECT * FROM ts_combine_keys('sales_data', sale_date, quantity, region_id, store_id, item_id)
ORDER BY unique_id, sale_date
LIMIT 10;

-- Combination with 2 columns (store-level granularity)
SELECT '--- 2-column combination (region|store) ---' AS subsection;
SELECT * FROM ts_combine_keys('sales_data', sale_date, quantity, region_id, store_id)
ORDER BY unique_id, sale_date
LIMIT 10;

-- Custom separator
SELECT '--- Custom separator ---' AS subsection;
SELECT * FROM ts_combine_keys('sales_data', sale_date, quantity, region_id, store_id, item_id,
    params := {'separator': '-'})
ORDER BY unique_id, sale_date
LIMIT 5;

-- =============================================================================
-- Section 3: Hierarchical Aggregation (Main Function)
-- =============================================================================
-- Create aggregated series at all hierarchy levels automatically.

SELECT '=== Section 3: Hierarchical Aggregation ===' AS section;

-- Full hierarchical aggregation
SELECT '--- All aggregation levels ---' AS subsection;
SELECT * FROM ts_aggregate_hierarchy('sales_data', sale_date, quantity, region_id, store_id, item_id)
ORDER BY unique_id, date_col;

-- Summary: count series at each level
SELECT '--- Count by aggregation level ---' AS subsection;
WITH agg AS (
    SELECT * FROM ts_aggregate_hierarchy('sales_data', sale_date, quantity, region_id, store_id, item_id)
)
SELECT
    CASE
        WHEN unique_id = 'AGGREGATED|AGGREGATED|AGGREGATED' THEN 'Level 0: Grand Total'
        WHEN unique_id LIKE '%|AGGREGATED|AGGREGATED' AND unique_id NOT LIKE 'AGGREGATED%' THEN 'Level 1: Per Region'
        WHEN unique_id LIKE '%|AGGREGATED' AND unique_id NOT LIKE '%|AGGREGATED|AGGREGATED' THEN 'Level 2: Per Store'
        ELSE 'Level 3: Original Items'
    END AS level,
    COUNT(DISTINCT unique_id) AS n_series,
    SUM(value_col) AS total_value
FROM agg
GROUP BY 1
ORDER BY 1;

-- Custom aggregate keyword
SELECT '--- Custom aggregate keyword ---' AS subsection;
SELECT DISTINCT unique_id
FROM ts_aggregate_hierarchy('sales_data', sale_date, quantity, region_id, store_id, item_id,
    params := {'aggregate_keyword': 'TOTAL'})
WHERE unique_id LIKE '%TOTAL%'
ORDER BY unique_id;

-- =============================================================================
-- Section 4: Split Keys (Reverse Operation)
-- =============================================================================
-- Split combined unique_id back into original columns.

SELECT '=== Section 4: Split Keys ===' AS section;

-- First, create some "forecast results" with combined keys
CREATE OR REPLACE TABLE forecast_results AS
SELECT
    unique_id,
    date_col AS forecast_date,
    value_col * 1.1 AS point_forecast  -- Simulated forecast
FROM ts_aggregate_hierarchy('sales_data', sale_date, quantity, region_id, store_id, item_id)
WHERE date_col = DATE '2024-01-03';

SELECT '--- Forecast results (combined keys) ---' AS subsection;
SELECT * FROM forecast_results ORDER BY unique_id LIMIT 10;

-- Split back into original columns
SELECT '--- Split keys back ---' AS subsection;
SELECT * FROM ts_split_keys('forecast_results', unique_id, forecast_date, point_forecast)
ORDER BY id_part_1, id_part_2, id_part_3
LIMIT 10;

-- Rename columns for clarity
SELECT '--- With renamed columns ---' AS subsection;
SELECT
    id_part_1 AS region_id,
    id_part_2 AS store_id,
    id_part_3 AS item_id,
    date_col AS forecast_date,
    value_col AS point_forecast
FROM ts_split_keys('forecast_results', unique_id, forecast_date, point_forecast)
ORDER BY region_id, store_id, item_id
LIMIT 10;

-- =============================================================================
-- Section 5: End-to-End Workflow
-- =============================================================================
-- Complete workflow: raw data -> aggregation -> forecast -> split

SELECT '=== Section 5: End-to-End Workflow ===' AS section;

-- Step 1: Create aggregated time series
SELECT '--- Step 1: Aggregate hierarchy ---' AS subsection;
CREATE OR REPLACE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy('sales_data', sale_date, quantity, region_id, store_id, item_id);

SELECT COUNT(DISTINCT unique_id) AS n_series, COUNT(*) AS n_observations
FROM prepared_data;

-- Step 2: Forecast all series (simulated here with simple model)
SELECT '--- Step 2: Generate forecasts ---' AS subsection;
CREATE OR REPLACE TABLE forecasts AS
SELECT
    unique_id AS id,
    1 AS forecast_step,
    DATE '2024-01-04' AS ds,
    AVG(value_col) * 1.05 AS point_forecast
FROM prepared_data
GROUP BY unique_id;

SELECT * FROM forecasts ORDER BY id LIMIT 10;

-- Step 3: Split keys for analysis
SELECT '--- Step 3: Split for analysis ---' AS subsection;
SELECT
    id_part_1 AS region_id,
    id_part_2 AS store_id,
    id_part_3 AS item_id,
    date_col AS forecast_date,
    ROUND(value_col, 2) AS point_forecast
FROM ts_split_keys('forecasts', id, ds, point_forecast)
ORDER BY region_id, store_id, item_id;

-- Step 4: Filter to specific level
SELECT '--- Step 4: Filter to store-level forecasts ---' AS subsection;
SELECT
    id_part_1 AS region_id,
    id_part_2 AS store_id,
    ROUND(value_col, 2) AS store_forecast
FROM ts_split_keys('forecasts', id, ds, point_forecast)
WHERE id_part_3 = 'AGGREGATED' AND id_part_2 != 'AGGREGATED'
ORDER BY region_id, store_id;

SELECT '=== Examples Complete ===' AS section;

-- Cleanup
DROP TABLE IF EXISTS sales_data;
DROP TABLE IF EXISTS conflicting_data;
DROP TABLE IF EXISTS forecast_results;
DROP TABLE IF EXISTS prepared_data;
DROP TABLE IF EXISTS forecasts;
