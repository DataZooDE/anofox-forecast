-- ============================================================================
-- CHANGEPOINT DETECTION EXAMPLE
-- ============================================================================
-- Demonstrates automatic detection of changepoints in time series data
-- using Bayesian Online Changepoint Detection (BOCPD)
-- ============================================================================

.timer on

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- STEP 1: Create time series with known changepoints
-- ============================================================================
SELECT '=== STEP 1: Creating time series with level shifts ===' AS step;

DROP TABLE IF EXISTS cp_data;
CREATE TABLE cp_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS ds,
    CASE 
        WHEN d < 50 THEN 100 + (RANDOM() * 5)
        WHEN d < 120 THEN 200 + (RANDOM() * 5)
        WHEN d < 200 THEN 150 + (RANDOM() * 5)
        ELSE 250 + (RANDOM() * 5)
    END AS y
FROM generate_series(0, 299) t(d);

SELECT 
    COUNT(*) AS total_points,
    ROUND(MIN(y), 2) AS min_value,
    ROUND(MAX(y), 2) AS max_value,
    ROUND(AVG(y), 2) AS avg_value
FROM cp_data;

-- ============================================================================
-- STEP 2: Simple changepoint detection (TS_DETECT_CHANGEPOINTS)
-- ============================================================================
SELECT '=== STEP 2: Detect changepoints (single series) ===' AS step;

-- NEW SIMPLE API - No UNNEST needed!
-- Use default parameters (hazard_lambda=250)
SELECT 
    date_col AS ds,
    value_col AS y,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, MAP{})
WHERE is_changepoint = true
ORDER BY ds;

-- Try with more sensitive detection (lower hazard_lambda = expects more changepoints)
SELECT '=== STEP 2b: Sensitive detection (hazard_lambda=50) ===' AS step;
SELECT 
    date_col AS ds,
    value_col AS y,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, {'hazard_lambda': 50.0})
WHERE is_changepoint = true
ORDER BY ds;

-- ============================================================================
-- STEP 3: View data around changepoints
-- ============================================================================
SELECT '=== STEP 3: Data around changepoints ===' AS step;

WITH changepoints AS (
    SELECT date_col AS ds
    FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, MAP{})
    WHERE is_changepoint = true
),
context AS (
    SELECT 
        d.ds,
        d.y,
        c.ds AS cp_date,
        DATE_DIFF('day', c.ds, d.ds) AS days_from_cp
    FROM cp_data d
    CROSS JOIN changepoints c
    WHERE ABS(DATE_DIFF('day', c.ds, d.ds)) <= 5
)
SELECT 
    ds,
    ROUND(y, 2) AS value,
    cp_date AS changepoint_date,
    days_from_cp
FROM context
ORDER BY cp_date, days_from_cp
LIMIT 30;

-- ============================================================================
-- STEP 4: Segment analysis (identify stable periods)
-- ============================================================================
SELECT '=== STEP 4: Segment statistics ===' AS step;

WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, MAP{})
),
segments AS (
    SELECT 
        date_col AS ds,
        value_col AS y,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (ORDER BY date_col) AS segment_id
    FROM changepoint_data
)
SELECT 
    segment_id,
    MIN(ds) AS segment_start,
    MAX(ds) AS segment_end,
    COUNT(*) AS segment_length,
    ROUND(AVG(y), 2) AS avg_value,
    ROUND(STDDEV(y), 2) AS std_dev
FROM segments
GROUP BY segment_id
ORDER BY segment_id;

-- ============================================================================
-- STEP 5: Multiple series with GROUP BY
-- ============================================================================
SELECT '=== STEP 5: Detect changepoints for multiple series ===' AS step;

DROP TABLE IF EXISTS multi_cp_data;
CREATE TABLE multi_cp_data AS
WITH RECURSIVE
    series_ids AS (
        SELECT 1 AS id
        UNION ALL
        SELECT id + 1 FROM series_ids WHERE id < 5
    ),
    dates AS (
        SELECT DATE '2023-01-01' + INTERVAL (d) DAY AS ds
        FROM generate_series(0, 199) t(d)
    )
SELECT 
    id,
    ds,
    CASE 
        -- Series 1: One major shift at day 100
        WHEN id = 1 AND ds < '2023-04-11' THEN 100 + (RANDOM() * 10)
        WHEN id = 1 THEN 200 + (RANDOM() * 10)
        -- Series 2: Two shifts
        WHEN id = 2 AND ds < '2023-03-01' THEN 150 + (RANDOM() * 10)
        WHEN id = 2 AND ds < '2023-05-01' THEN 100 + (RANDOM() * 10)
        WHEN id = 2 THEN 250 + (RANDOM() * 10)
        -- Series 3: Gradual trend (no sharp changes)
        WHEN id = 3 THEN 100 + EXTRACT(DAY FROM ds) * 0.5 + (RANDOM() * 10)
        -- Series 4: Multiple small shifts
        WHEN id = 4 THEN 100 + FLOOR(EXTRACT(DAY FROM ds) / 50) * 30 + (RANDOM() * 10)
        -- Series 5: Stable (no changepoints)
        ELSE 100 + (RANDOM() * 10)
    END AS y
FROM series_ids, dates;

SELECT id, COUNT(*) AS num_points
FROM multi_cp_data
GROUP BY id
ORDER BY id;

-- ============================================================================
-- STEP 6: Detect changepoints for each series
-- ============================================================================
SELECT '=== STEP 6: Changepoints per series ===' AS step;

-- NEW SIMPLE API WITH GROUP BY!
-- Default parameters
SELECT 
    id,
    date_col AS ds,
    value_col AS y,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
WHERE is_changepoint = true
ORDER BY id, ds;

-- More sensitive detection for series with subtle changes
SELECT '=== STEP 6b: More sensitive detection ===' AS step;
SELECT 
    id,
    COUNT(*) FILTER (WHERE is_changepoint) AS changepoints_detected
FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, {'hazard_lambda': 100.0})
GROUP BY id
ORDER BY id;

-- ============================================================================
-- STEP 7: Count changepoints per series
-- ============================================================================
SELECT '=== STEP 7: Changepoint summary by series ===' AS step;

WITH cp_data AS (
    SELECT 
        id,
        is_changepoint
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
)
SELECT 
    id,
    COUNT(CASE WHEN is_changepoint THEN 1 END) AS changepoint_count,
    CASE 
        WHEN COUNT(CASE WHEN is_changepoint THEN 1 END) = 0 THEN 'Stable'
        WHEN COUNT(CASE WHEN is_changepoint THEN 1 END) <= 3 THEN 'Few shifts'
        WHEN COUNT(CASE WHEN is_changepoint THEN 1 END) <= 6 THEN 'Moderate shifts'
        ELSE 'Many shifts'
    END AS classification
FROM cp_data
GROUP BY id
ORDER BY id;

-- ============================================================================
-- STEP 8: Forecast only recent stable period (SINGLE & MULTIPLE SERIES)
-- ============================================================================
SELECT '=== STEP 8a: Forecast after last changepoint (single series) ===' AS step;

-- Find the last changepoint
WITH last_cp AS (
    SELECT MAX(date_col) AS last_changepoint_date
    FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, MAP{})
    WHERE is_changepoint = true
)
SELECT 
    'Last changepoint detected at: ' || (SELECT last_changepoint_date FROM last_cp) AS info;

-- Forecast using only data after the last changepoint
WITH last_cp AS (
    SELECT MAX(date_col) AS last_changepoint_date
    FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, MAP{})
    WHERE is_changepoint = true
),
stable_period AS (
    SELECT ds, y
    FROM cp_data 
    WHERE ds > (SELECT last_changepoint_date FROM last_cp)
)
SELECT 
    forecast_step,
    date_col AS forecast_date,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_bound,
    ROUND(upper, 2) AS upper_bound
FROM TS_FORECAST('stable_period', ds, y, 'Naive', 14, MAP{})
LIMIT 7;

-- STEP 8b: Forecast for multiple series using stable periods
SELECT '=== STEP 8b: Adaptive forecasting for multiple series ===' AS step;

-- Find last changepoint for each series and count stable observations
WITH changepoint_info AS (
    SELECT 
        id,
        MAX(date_col) FILTER (WHERE is_changepoint) AS last_cp_date,
        COUNT(*) AS total_points,
        COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
    GROUP BY id
),
stable_periods AS (
    SELECT 
        m.id,
        m.ds,
        COALESCE(ci.last_cp_date, MIN(m.ds) OVER (PARTITION BY m.id)) AS regime_start,
        ci.num_changepoints
    FROM multi_cp_data m
    INNER JOIN changepoint_info ci ON m.id = ci.id
    WHERE m.ds > COALESCE(ci.last_cp_date, DATE '1900-01-01')
)
SELECT 
    id,
    regime_start,
    COUNT(*) AS stable_observations,
    num_changepoints,
    CASE 
        WHEN COUNT(*) >= 30 THEN '✅ Sufficient data for forecasting'
        WHEN COUNT(*) >= 14 THEN '⚠️ Limited data'
        ELSE '❌ Too few observations'
    END AS forecast_viability
FROM stable_periods
GROUP BY id, regime_start, num_changepoints
ORDER BY id;

-- ============================================================================
-- STEP 9: Create regime column as a feature for forecasting
-- ============================================================================
SELECT '=== STEP 9: Regime column for ML features ===' AS step;

-- STEP 9a: Single series - Add regime_id column
SELECT '=== STEP 9a: Single series with regime_id ===' AS substep;

WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, MAP{})
),
with_regime AS (
    SELECT 
        date_col AS ds,
        value_col AS y,
        is_changepoint,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (ORDER BY date_col) AS regime_id
    FROM changepoint_data
)
SELECT 
    ds,
    ROUND(y, 2) AS y,
    regime_id,
    is_changepoint
FROM with_regime
ORDER BY ds
LIMIT 20;

-- STEP 9b: Multiple series - Add regime_id per series
SELECT '=== STEP 9b: Multiple series with regime_id (GROUP BY) ===' AS substep;

WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
),
with_regime AS (
    SELECT 
        id,
        date_col AS ds,
        value_col AS y,
        is_changepoint,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY id ORDER BY date_col) AS regime_id
    FROM changepoint_data
)
SELECT 
    id,
    ds,
    ROUND(y, 2) AS y,
    regime_id,
    is_changepoint
FROM with_regime
WHERE id <= 2  -- Show first 2 series
ORDER BY id, ds
LIMIT 30;

-- STEP 9c: Regime statistics (useful for feature engineering)
SELECT '=== STEP 9c: Regime characteristics by series ===' AS substep;

WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
),
with_regime AS (
    SELECT 
        id,
        date_col AS ds,
        value_col AS y,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY id ORDER BY date_col) AS regime_id
    FROM changepoint_data
)
SELECT 
    id,
    regime_id,
    MIN(ds) AS regime_start,
    MAX(ds) AS regime_end,
    COUNT(*) AS regime_length,
    ROUND(AVG(y), 2) AS regime_mean,
    ROUND(STDDEV(y), 2) AS regime_std,
    ROUND(MIN(y), 2) AS regime_min,
    ROUND(MAX(y), 2) AS regime_max
FROM with_regime
GROUP BY id, regime_id
ORDER BY id, regime_id;

-- STEP 9d: Create table with regime column for future use
SELECT '=== STEP 9d: Persist regime-enriched data ===' AS substep;

DROP TABLE IF EXISTS multi_cp_data_with_regimes;
CREATE TABLE multi_cp_data_with_regimes AS
WITH changepoint_data AS (
    SELECT *
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
),
with_regime AS (
    SELECT 
        id,
        date_col AS ds,
        value_col AS y,
        is_changepoint,
        SUM(CASE WHEN is_changepoint THEN 1 ELSE 0 END) 
            OVER (PARTITION BY id ORDER BY date_col) AS regime_id
    FROM changepoint_data
)
SELECT 
    id,
    ds,
    y,
    regime_id,
    is_changepoint
FROM with_regime;

SELECT 
    'Created table with ' || COUNT(*) || ' rows, ' || 
    COUNT(DISTINCT id) || ' series, ' ||
    COUNT(DISTINCT regime_id) || ' total regimes' AS summary
FROM multi_cp_data_with_regimes;

-- Show sample
SELECT * FROM multi_cp_data_with_regimes 
WHERE id = 1 
ORDER BY ds 
LIMIT 15;

-- ============================================================================
-- STEP 10: Changepoint probabilities (OPTIONAL FEATURE)
-- ============================================================================
SELECT '=== STEP 10: Using changepoint probabilities ===' AS step;

-- STEP 10a: Default - probabilities are 0.0 (faster, no computation)
SELECT '=== STEP 10a: Default (probabilities = 0) ===' AS substep;
SELECT 
    date_col AS ds,
    ROUND(value_col, 2) AS y,
    is_changepoint,
    changepoint_probability AS cp_prob
FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, MAP{})
WHERE is_changepoint = true
LIMIT 5;

-- STEP 10b: Enable probability computation
SELECT '=== STEP 10b: With probabilities enabled ===' AS substep;
SELECT 
    date_col AS ds,
    ROUND(value_col, 2) AS y,
    is_changepoint,
    ROUND(changepoint_probability, 4) AS cp_prob
FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, {'include_probabilities': true})
WHERE changepoint_probability > 0.001
ORDER BY changepoint_probability DESC
LIMIT 15;

-- STEP 10c: Find uncertain changepoints (low probability)
SELECT '=== STEP 10c: Uncertain vs confident changepoints ===' AS substep;
WITH cp_with_probs AS (
    SELECT 
        date_col,
        value_col,
        is_changepoint,
        changepoint_probability
    FROM TS_DETECT_CHANGEPOINTS('cp_data', ds, y, {'include_probabilities': true})
)
SELECT 
    CASE 
        WHEN changepoint_probability >= 0.1 THEN 'High confidence'
        WHEN changepoint_probability >= 0.01 THEN 'Medium confidence'
        WHEN changepoint_probability >= 0.001 THEN 'Low confidence'
        ELSE 'Very low/No change'
    END AS confidence_level,
    COUNT(*) AS num_points,
    COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
FROM cp_with_probs
GROUP BY confidence_level
ORDER BY MIN(changepoint_probability) DESC;

-- STEP 10d: Multiple series - identify high-probability changepoints
SELECT '=== STEP 10d: High-confidence changepoints (GROUP BY) ===' AS substep;
SELECT 
    id,
    date_col AS ds,
    ROUND(changepoint_probability, 4) AS cp_prob,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, {'include_probabilities': true})
WHERE changepoint_probability > 0.005
ORDER BY changepoint_probability DESC
LIMIT 20;

-- ============================================================================
-- STEP 11: Alert on recent changepoints (MULTIPLE SERIES)
-- ============================================================================
SELECT '=== STEP 11a: Changepoint alerts by series ===' AS step;

WITH recent_changepoints AS (
    SELECT 
        id,
        MAX(date_col) FILTER (WHERE is_changepoint) AS last_changepoint,
        DATE '2023-12-31' AS today  -- Using end of data range
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
    GROUP BY id
)
SELECT 
    id,
    last_changepoint,
    DATE_DIFF('day', last_changepoint, today) AS days_since_change,
    CASE 
        WHEN DATE_DIFF('day', last_changepoint, today) <= 7 THEN '🔴 RECENT CHANGE'
        WHEN DATE_DIFF('day', last_changepoint, today) <= 30 THEN '🟡 CHANGE LAST MONTH'
        ELSE '🟢 STABLE'
    END AS alert_status
FROM recent_changepoints
WHERE last_changepoint IS NOT NULL
ORDER BY days_since_change;

-- STEP 11b: Changepoint summary statistics
SELECT '=== STEP 11b: Changepoint magnitude by series ===' AS step;

WITH cp_data_all AS (
    SELECT 
        id,
        date_col,
        value_col,
        is_changepoint
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
),
cp_analysis AS (
    SELECT 
        id,
        date_col,
        value_col,
        is_changepoint,
        LAG(value_col) OVER (PARTITION BY id ORDER BY date_col) AS prev_value
    FROM cp_data_all
),
changepoint_magnitudes AS (
    SELECT 
        id,
        date_col AS changepoint_date,
        value_col,
        prev_value,
        ABS(value_col - prev_value) AS magnitude
    FROM cp_analysis
    WHERE is_changepoint = true AND prev_value IS NOT NULL
)
SELECT 
    id,
    COUNT(*) AS num_changepoints,
    ROUND(AVG(magnitude), 2) AS avg_magnitude,
    ROUND(MAX(magnitude), 2) AS max_magnitude,
    MAX(changepoint_date) AS last_changepoint
FROM changepoint_magnitudes
GROUP BY id
ORDER BY num_changepoints DESC;

-- STEP 11c: Identify series with concerning patterns
SELECT '=== STEP 11c: Series requiring attention ===' AS step;

WITH cp_counts AS (
    SELECT 
        id,
        COUNT(*) FILTER (WHERE is_changepoint) AS changepoint_count,
        MAX(date_col) AS last_observation,
        MAX(date_col) FILTER (WHERE is_changepoint) AS last_changepoint
    FROM TS_DETECT_CHANGEPOINTS_BY('multi_cp_data', id, ds, y, MAP{})
    GROUP BY id
)
SELECT 
    id,
    changepoint_count,
    last_changepoint,
    CASE 
        WHEN changepoint_count >= 5 THEN '🔴 HIGH VOLATILITY - Review urgently'
        WHEN changepoint_count >= 3 THEN '🟡 MODERATE CHANGES - Monitor closely'
        WHEN changepoint_count <= 1 THEN '🟢 STABLE - No action needed'
        ELSE '⚪ NORMAL VARIABILITY'
    END AS recommendation,
    CASE 
        WHEN last_changepoint IS NOT NULL 
        THEN DATE_DIFF('day', last_changepoint, last_observation) || ' days in current regime'
        ELSE 'No changepoints detected'
    END AS regime_stability
FROM cp_counts
ORDER BY changepoint_count DESC, id;

-- ============================================================================
-- STEP 12: Using regime column for forecasting and analysis
-- ============================================================================
SELECT '=== STEP 12: Practical applications of regime features ===' AS step;

-- STEP 12a: Forecast separately per regime (when regimes have different behavior)
SELECT '=== STEP 12a: Regime-specific statistics ===' AS substep;

SELECT 
    id,
    regime_id,
    COUNT(*) AS observations,
    ROUND(AVG(y), 2) AS mean,
    ROUND(STDDEV(y), 2) AS std_dev,
    ROUND(MAX(y) - MIN(y), 2) AS range
FROM multi_cp_data_with_regimes
GROUP BY id, regime_id
HAVING COUNT(*) >= 10  -- Only regimes with sufficient data
ORDER BY id, regime_id;

-- STEP 12b: Identify current regime for each series
SELECT '=== STEP 12b: Current regime identification ===' AS substep;

WITH current_regime AS (
    SELECT 
        id,
        MAX(regime_id) AS current_regime_id,
        MAX(ds) AS last_observation
    FROM multi_cp_data_with_regimes
    GROUP BY id
)
SELECT 
    cr.id,
    cr.current_regime_id,
    cr.last_observation,
    COUNT(*) AS points_in_current_regime,
    ROUND(AVG(m.y), 2) AS current_regime_mean
FROM current_regime cr
INNER JOIN multi_cp_data_with_regimes m 
    ON cr.id = m.id AND cr.current_regime_id = m.regime_id
GROUP BY cr.id, cr.current_regime_id, cr.last_observation
ORDER BY cr.id;

-- STEP 12c: Feature engineering example - Regime duration
SELECT '=== STEP 12c: Regime-based features for ML ===' AS substep;

WITH regime_features AS (
    SELECT 
        id,
        ds,
        y,
        regime_id,
        -- Regime-level features
        ROW_NUMBER() OVER (PARTITION BY id, regime_id ORDER BY ds) AS days_in_regime,
        AVG(y) OVER (PARTITION BY id, regime_id) AS regime_mean,
        STDDEV(y) OVER (PARTITION BY id, regime_id) AS regime_volatility,
        -- Previous regime comparison
        LAG(regime_id) OVER (PARTITION BY id ORDER BY ds) AS prev_regime_id,
        -- Time since last changepoint
        ds - MAX(CASE WHEN is_changepoint THEN ds END) 
            OVER (PARTITION BY id ORDER BY ds) AS days_since_changepoint
    FROM multi_cp_data_with_regimes
)
SELECT 
    id,
    ds,
    ROUND(y, 2) AS y,
    regime_id,
    days_in_regime,
    ROUND(regime_mean, 2) AS regime_mean,
    ROUND(regime_volatility, 2) AS regime_vol,
    CASE 
        WHEN days_since_changepoint IS NOT NULL 
        THEN EXTRACT(DAY FROM days_since_changepoint)
        ELSE NULL
    END AS days_stable
FROM regime_features
WHERE id = 1
ORDER BY ds
LIMIT 20;

-- STEP 12d: Regime transition matrix (for understanding dynamics)
SELECT '=== STEP 12d: Regime transition patterns ===' AS substep;

WITH regime_transitions AS (
    SELECT 
        id,
        regime_id AS current_regime,
        LAG(regime_id) OVER (PARTITION BY id ORDER BY ds) AS previous_regime,
        ds AS transition_date
    FROM multi_cp_data_with_regimes
    WHERE is_changepoint = true
),
first_transition AS (
    SELECT MIN(transition_date) AS start_date FROM regime_transitions
)
SELECT 
    current_regime,
    COUNT(*) AS num_transitions,
    COUNT(DISTINCT id) AS num_series,
    ROUND(AVG(DATE_DIFF('day', (SELECT start_date FROM first_transition), transition_date)), 1) AS avg_days_from_start
FROM regime_transitions
WHERE previous_regime IS NOT NULL
GROUP BY current_regime
ORDER BY current_regime;

.timer off

-- ============================================================================
-- KEY TAKEAWAYS
-- ============================================================================
-- 1. TS_DETECT_CHANGEPOINTS(table, date_col, value_col, params) - single series
-- 2. TS_DETECT_CHANGEPOINTS_BY(table, group_col, date_col, value_col, params) - multiple series
-- 3. Parameters (all optional):
--    - hazard_lambda: Expected run length between changepoints (default: 250.0)
--                     Lower values = more sensitive (more changepoints detected)
--                     Higher values = less sensitive (fewer changepoints)
-- 4. Returns training data with is_changepoint boolean column
-- 5. No UNNEST needed - clean table output!
-- 6. Works with GROUP BY for analyzing multiple series in parallel
-- 7. Use for: anomaly detection, segmentation, adaptive forecasting
-- 8. Based on BOCPD (Bayesian Online Changepoint Detection)
-- 9. REGIME COLUMN: Create regime_id using window function SUM(CASE WHEN is_changepoint...)
-- 10. Use regime_id as a feature for:
--     - Feature engineering in ML models
--     - Regime-specific forecasting
--     - Transition analysis
--     - Stability monitoring
-- 11. CHANGEPOINT PROBABILITIES: Set {'include_probabilities': true} to get probability scores
--     - Default: probabilities = 0.0 (faster, no extra computation)
--     - Enabled: probabilities between 0 and 1 (higher = more confident changepoint)
--     - Use for: filtering uncertain changepoints, confidence scoring
-- ============================================================================

