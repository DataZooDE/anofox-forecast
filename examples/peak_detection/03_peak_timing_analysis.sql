-- =============================================================================
-- Peak Timing Analysis with ts_analyze_peak_timing
-- =============================================================================
-- This example demonstrates how to analyze the consistency and patterns of
-- peak timing within seasonal cycles.
--
-- Key concepts:
--   - Peak timing measures WHEN peaks occur within each cycle
--   - Consistent timing = peaks always occur at same point in cycle
--   - Variable timing = peaks shift around within cycles
--   - Useful for: demand planning, capacity scheduling, anomaly detection
-- =============================================================================

-- Load the sample data first:
-- .read examples/peak_detection/01_sample_data.sql

-- -----------------------------------------------------------------------------
-- Example 1: Analyze daily peak timing in website traffic
-- -----------------------------------------------------------------------------
-- Period = 24 hours, check if peaks occur at consistent times each day

SELECT 'Website Traffic - Daily Peak Timing Consistency' AS example;

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
    ROUND(timing.mean_timing * 24, 1) AS mean_peak_hour,  -- Convert normalized to hours
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


-- -----------------------------------------------------------------------------
-- Example 2: Yearly peak timing in sales data
-- -----------------------------------------------------------------------------
-- Period = 12 months, analyze when yearly peaks typically occur

SELECT 'Monthly Sales - Yearly Peak Timing Analysis' AS example;

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
    ROUND(timing.mean_timing * 12, 1) AS mean_peak_month_offset,  -- 0-based month in year
    ROUND(timing.mean_timing * 12 + 1, 0)::INT AS typical_peak_month,  -- 1-based
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


-- -----------------------------------------------------------------------------
-- Example 3: Heart rate regularity analysis
-- -----------------------------------------------------------------------------
-- Period = 100 samples (~1 second at 100Hz), check heartbeat regularity

SELECT 'Heart Rate - Beat Timing Regularity' AS example;

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
    ROUND(timing.mean_timing * 100, 1) AS mean_beat_position,  -- Position within 1-second window
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


-- -----------------------------------------------------------------------------
-- Example 4: Full timing analysis details
-- -----------------------------------------------------------------------------
-- Show all timing analysis metrics

SELECT 'Full Timing Analysis - Website Traffic' AS example;

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
    timing.normalized_timing AS normalized_positions,  -- 0-1 scale within each cycle
    ROUND(timing.mean_timing, 4) AS mean_normalized_timing,
    ROUND(timing.std_timing, 4) AS std_normalized_timing,
    ROUND(timing.range_timing, 4) AS timing_range,
    ROUND(timing.variability_score, 4) AS variability_score,
    ROUND(timing.timing_trend, 4) AS timing_trend,  -- Positive = peaks getting later
    timing.is_stable AS is_stable
FROM result;


-- -----------------------------------------------------------------------------
-- Example 5: Timing trend interpretation
-- -----------------------------------------------------------------------------
-- Check if peaks are drifting earlier or later over time

SELECT 'Peak Timing Trend Analysis' AS example;

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


-- -----------------------------------------------------------------------------
-- Example 6: Combining peak detection and timing analysis
-- -----------------------------------------------------------------------------
-- Full analysis: detect peaks, then analyze their timing patterns

SELECT 'Combined Peak Detection and Timing Analysis' AS example;

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


-- -----------------------------------------------------------------------------
-- Example 7: Compare timing across different subsets
-- -----------------------------------------------------------------------------
-- Analyze if peak timing differs between weekdays and weekends

SELECT 'Timing Comparison - Weekdays vs Weekends' AS example;

-- Note: This requires enough data per subset for reliable timing analysis
-- With 7 days of hourly data, we have limited cycles per subset

WITH weekday_hours AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
    WHERE EXTRACT(DOW FROM timestamp) BETWEEN 1 AND 5  -- Mon-Fri
),
weekend_hours AS (
    SELECT LIST(visitors ORDER BY timestamp) AS values
    FROM website_traffic
    WHERE EXTRACT(DOW FROM timestamp) IN (0, 6)  -- Sat-Sun
)
SELECT
    'Weekdays' AS period,
    (ts_analyze_peak_timing(values, 24)).n_peaks AS peaks,
    ROUND((ts_analyze_peak_timing(values, 24)).mean_timing * 24, 1) AS mean_peak_hour,
    (ts_analyze_peak_timing(values, 24)).is_stable AS stable
FROM weekday_hours
UNION ALL
SELECT
    'Weekends',
    (ts_analyze_peak_timing(values, 24)).n_peaks,
    ROUND((ts_analyze_peak_timing(values, 24)).mean_timing * 24, 1),
    (ts_analyze_peak_timing(values, 24)).is_stable
FROM weekend_hours;
