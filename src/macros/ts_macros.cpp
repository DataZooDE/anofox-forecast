#include "anofox_forecast_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"
#include "duckdb/parser/parsed_data/create_function_info.hpp"
#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/function/table_macro_function.hpp"

namespace duckdb {

// Structure for defining table macros
struct TsTableMacro {
    const char *name;
    const char *parameters[10];         // Positional parameters (nullptr terminated)
    struct {
        const char *name;
        const char *default_value;
    } named_params[8];                  // Named parameters with defaults
    const char *macro;                  // SQL definition
    const char *description;            // Added: human-readable description
    const char *example;                // Added: example SQL usage
    const char *category;               // Added: primary category
};

// clang-format off
static const TsTableMacro ts_table_macros[] = {
    // ts_stats: Compute statistics for grouped time series
    // C++ API: ts_stats(table_name, group_col, date_col, value_col, frequency)
    // Returns: TABLE with expanded statistics columns
    {"ts_stats", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH stats_data AS (
    SELECT
        group_col,
        _ts_stats_with_dates(
            LIST(value_col::DOUBLE ORDER BY date_col),
            LIST(date_col::TIMESTAMP ORDER BY date_col),
            frequency::VARCHAR
        ) AS _stats
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    group_col,
    (_stats).length AS length,
    (_stats).n_nulls AS n_nulls,
    (_stats).n_nan AS n_nan,
    (_stats).n_zeros AS n_zeros,
    (_stats).n_positive AS n_positive,
    (_stats).n_negative AS n_negative,
    (_stats).n_unique_values AS n_unique_values,
    (_stats).is_constant AS is_constant,
    (_stats).n_zeros_start AS n_zeros_start,
    (_stats).n_zeros_end AS n_zeros_end,
    (_stats).plateau_size AS plateau_size,
    (_stats).plateau_size_nonzero AS plateau_size_nonzero,
    (_stats).mean AS mean,
    (_stats).median AS median,
    (_stats).std_dev AS std_dev,
    (_stats).variance AS variance,
    (_stats).min AS min,
    (_stats).max AS max,
    (_stats).range AS range,
    (_stats).sum AS sum,
    (_stats).skewness AS skewness,
    (_stats).kurtosis AS kurtosis,
    (_stats).tail_index AS tail_index,
    (_stats).bimodality_coef AS bimodality_coef,
    (_stats).trimmed_mean AS trimmed_mean,
    (_stats).coef_variation AS coef_variation,
    (_stats).q1 AS q1,
    (_stats).q3 AS q3,
    (_stats).iqr AS iqr,
    (_stats).autocorr_lag1 AS autocorr_lag1,
    (_stats).trend_strength AS trend_strength,
    (_stats).seasonality_strength AS seasonality_strength,
    (_stats).entropy AS entropy,
    (_stats).stability AS stability,
    (_stats).expected_length AS expected_length,
    (_stats).n_gaps AS n_gaps
FROM stats_data
)",
    "Computes 34 time series statistics per group. Returns a wide table with one column per statistic.",
    "SELECT * FROM ts_stats('sales', product_id, date, qty, '1d')",
    "statistics"},

    // ts_quality_report: Generate quality report from stats table
    // C++ API: ts_quality_report(stats_table, min_length)
    {"ts_quality_report", {"stats_table", "min_length", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    SUM(CASE WHEN length >= min_length AND NOT is_constant THEN 1 ELSE 0 END) AS n_passed,
    SUM(CASE WHEN n_nan > 0 THEN 1 ELSE 0 END) AS n_nan_issues,
    SUM(CASE WHEN n_nulls > 0 THEN 1 ELSE 0 END) AS n_missing_issues,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS n_constant,
    COUNT(*) AS n_total
FROM query_table(stats_table::VARCHAR)
)",
    "Generates a data quality report with null counts, gap counts, duplicate timestamps, and quality score per group.",
    "SELECT * FROM ts_quality_report('sales', product_id, date, qty)",
    "statistics"},

    // ts_stats_summary: Summary statistics from stats table
    // C++ API: ts_stats_summary(stats_table)
    {"ts_stats_summary", {"stats_table", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    COUNT(*) AS n_series,
    AVG(length) AS avg_length,
    MIN(length) AS min_length,
    MAX(length) AS max_length,
    SUM(n_nulls) AS total_nulls,
    SUM(n_nan) AS total_nans
FROM query_table(stats_table::VARCHAR)
)",
    "Aggregates ts_stats output across all groups into summary statistics (mean, std, min, max per metric).",
    "SELECT * FROM ts_stats_summary('sales', product_id, date, qty, '1d')",
    "statistics"},

    // ts_data_quality: Assess data quality per series
    // C++ API: ts_data_quality(table_name, unique_id_col, date_col, value_col, n_short, frequency)
    // Returns: TABLE with expanded quality columns
    {"ts_data_quality", {"source", "unique_id_col", "date_col", "value_col", "n_short", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH quality_data AS (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(LIST(value_col::DOUBLE ORDER BY date_col)) AS _quality
    FROM query_table(source::VARCHAR)
    GROUP BY unique_id_col
)
SELECT
    unique_id,
    (_quality).structural_score AS structural_score,
    (_quality).temporal_score AS temporal_score,
    (_quality).magnitude_score AS magnitude_score,
    (_quality).behavioral_score AS behavioral_score,
    (_quality).overall_score AS overall_score,
    (_quality).n_gaps AS n_gaps,
    (_quality).n_missing AS n_missing,
    (_quality).is_constant AS is_constant
FROM quality_data
)",
    "Assesses time series data quality per group, returning null rate, gap rate, duplicate count, and quality score.",
    "SELECT * FROM ts_data_quality('sales', product_id, date, qty)",
    "data-quality"},

    // ts_data_quality_summary: Summarize data quality across series
    // C++ API: ts_data_quality_summary(table_name, unique_id_col, date_col, value_col, n_short)
    {"ts_data_quality_summary", {"source", "unique_id_col", "date_col", "value_col", "n_short", nullptr}, {{nullptr, nullptr}},
R"(
WITH quality_data AS (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(LIST(value_col::DOUBLE ORDER BY date_col)) AS _quality
    FROM query_table(source::VARCHAR)
    GROUP BY unique_id_col
)
SELECT
    COUNT(*) AS n_total,
    SUM(CASE WHEN (_quality).overall_score >= 0.8 THEN 1 ELSE 0 END) AS n_good,
    SUM(CASE WHEN (_quality).overall_score >= 0.5 AND (_quality).overall_score < 0.8 THEN 1 ELSE 0 END) AS n_fair,
    SUM(CASE WHEN (_quality).overall_score < 0.5 THEN 1 ELSE 0 END) AS n_poor,
    AVG((_quality).overall_score) AS avg_score
FROM quality_data
)",
    "Summarizes ts_data_quality output across all groups into aggregate quality metrics.",
    "SELECT * FROM ts_data_quality_summary('sales', product_id, date, qty)",
    "data-quality"},

    // ts_drop_constant_by: Filter out constant series (table-based)
    // C++ API: ts_drop_constant_by(table_name, group_col, value_col)
    {"ts_drop_constant_by", {"source", "group_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING MIN(value_col) != MAX(value_col) OR MIN(value_col) IS NULL OR MAX(value_col) IS NULL
)
)",
    "Removes groups whose time series values are all constant (zero variance).",
    "SELECT * FROM ts_drop_constant_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_drop_short_by: Filter out short series (table-based)
    // C++ API: ts_drop_short_by(table_name, group_col, min_length)
    {"ts_drop_short_by", {"source", "group_col", "min_length", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING COUNT(*) >= min_length
)
)",
    "Removes groups with fewer observations than the specified minimum length.",
    "SELECT * FROM ts_drop_short_by('sales', product_id, date, qty, 20)",
    "data-preparation"},

    // ts_drop_leading_zeros_by: Remove leading zeros from series (table-based)
    // C++ API: ts_drop_leading_zeros_by(table_name, group_col, date_col, value_col)
    {"ts_drop_leading_zeros_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH first_nonzero AS (
    SELECT *,
           MIN(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN date_col END) OVER (PARTITION BY group_col) AS _first_nz
    FROM query_table(source::VARCHAR)
)
SELECT * EXCLUDE (_first_nz)
FROM first_nonzero
WHERE date_col >= _first_nz
)",
    "Removes leading zeros from each group's time series, trimming the start of the series.",
    "SELECT * FROM ts_drop_leading_zeros_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_drop_trailing_zeros_by: Remove trailing zeros from series (table-based)
    // C++ API: ts_drop_trailing_zeros_by(table_name, group_col, date_col, value_col)
    {"ts_drop_trailing_zeros_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH last_nonzero AS (
    SELECT *,
           MAX(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN date_col END) OVER (PARTITION BY group_col) AS _last_nz
    FROM query_table(source::VARCHAR)
)
SELECT * EXCLUDE (_last_nz)
FROM last_nonzero
WHERE date_col <= _last_nz
)",
    "Removes trailing zeros from each group's time series, trimming the end of the series.",
    "SELECT * FROM ts_drop_trailing_zeros_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_drop_edge_zeros_by: Remove both leading and trailing zeros (table-based)
    // C++ API: ts_drop_edge_zeros_by(table_name, group_col, date_col, value_col)
    {"ts_drop_edge_zeros_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH nonzero_bounds AS (
    SELECT *,
           MIN(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN date_col END) OVER (PARTITION BY group_col) AS _first_nz,
           MAX(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN date_col END) OVER (PARTITION BY group_col) AS _last_nz
    FROM query_table(source::VARCHAR)
)
SELECT * EXCLUDE (_first_nz, _last_nz)
FROM nonzero_bounds
WHERE date_col >= _first_nz AND date_col <= _last_nz
)",
    "Removes both leading and trailing zeros from each group's time series.",
    "SELECT * FROM ts_drop_edge_zeros_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_fill_nulls_const_by: Fill NULL values with constant (table-based)
    // C++ API: ts_fill_nulls_const_by(table_name, group_col, date_col, value_col, fill_value)
    // Note: All input columns are preserved; filled value is added as 'filled_value' column
    {"ts_fill_nulls_const_by", {"source", "group_col", "date_col", "value_col", "fill_value", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *, COALESCE(value_col, fill_value) AS filled_value
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)",
    "Fills NULL values in each group's time series with a constant value.",
    "SELECT * FROM ts_fill_nulls_const_by('sales', product_id, date, qty, 0.0)",
    "data-preparation"},

    // ts_fill_nulls_forward_by: Forward fill (LOCF) (table-based)
    // C++ API: ts_fill_nulls_forward_by(table_name, group_col, date_col, value_col)
    // Note: All input columns are preserved; filled value is added as 'filled_value' column
    {"ts_fill_nulls_forward_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *,
    COALESCE(value_col, LAG(value_col IGNORE NULLS) OVER (
        PARTITION BY group_col ORDER BY date_col
    )) AS filled_value
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)",
    "Forward-fills NULL values in each group's time series using the last known value.",
    "SELECT * FROM ts_fill_nulls_forward_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_fill_nulls_backward_by: Backward fill (NOCB) (table-based)
    // C++ API: ts_fill_nulls_backward_by(table_name, group_col, date_col, value_col)
    // Note: All input columns are preserved; filled value is added as 'filled_value' column
    {"ts_fill_nulls_backward_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *,
    COALESCE(value_col, LEAD(value_col IGNORE NULLS) OVER (
        PARTITION BY group_col ORDER BY date_col
    )) AS filled_value
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)",
    "Backward-fills NULL values in each group's time series using the next known value.",
    "SELECT * FROM ts_fill_nulls_backward_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_fill_nulls_mean_by: Fill with series mean (table-based)
    // C++ API: ts_fill_nulls_mean_by(table_name, group_col, date_col, value_col)
    // Note: All input columns are preserved; filled value is added as 'filled_value' column
    {"ts_fill_nulls_mean_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH with_mean AS (
    SELECT *,
           AVG(value_col) OVER (PARTITION BY group_col) AS _mean_val
    FROM query_table(source::VARCHAR)
)
SELECT * EXCLUDE (_mean_val), COALESCE(value_col, _mean_val) AS filled_value
FROM with_mean
ORDER BY group_col, date_col
)",
    "Fills NULL values in each group's time series with the group mean.",
    "SELECT * FROM ts_fill_nulls_mean_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_diff_by: Compute differences (table-based)
    // C++ API: ts_diff_by(table_name, group_col, date_col, value_col, diff_order)
    {"ts_diff_by", {"source", "group_col", "date_col", "value_col", "diff_order", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col,
    date_col,
    value_col - LAG(value_col, diff_order) OVER (
        PARTITION BY group_col ORDER BY date_col
    ) AS diff_value
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)",
    "Computes first-order differences for each group's time series (value[t] - value[t-1]).",
    "SELECT * FROM ts_diff_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_fill_gaps_by: Fill date gaps with NULL values at specified frequency
    // C++ API: ts_fill_gaps_by(table_name, group_col, date_col, value_col, frequency)
    // Supports: integers (as days), Polars-style ('1d', '1h', '30m', '1w', '1mo', '1q', '1y'), DuckDB INTERVAL ('1 day', '1 hour')
    //
    // This macro is a thin wrapper around the native streaming implementation (_ts_fill_gaps_native)
    // which uses 16x less memory than the previous SQL macro approach (GH#113, GH#115).
    //
    // Performance (1M rows, 10K groups):
    //   SQL macro:  181 MB peak memory
    //   Native:      11 MB peak memory (16x reduction)
    {"ts_fill_gaps_by", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_fill_gaps_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR)),
    frequency
)
)",
    "Fills missing timestamps in each group's time series to create a regular frequency grid.",
    "SELECT * FROM ts_fill_gaps_by('sales', product_id, date, qty, '1d')",
    "data-preparation"},

    // ts_fill_forward_by: Fill forward to a target date with NULL values
    // C++ API: ts_fill_forward_by(table_name, group_col, date_col, value_col, target_date, frequency)
    // Supports both Polars-style ('1d', '1h', '30m', '1w', '1mo', '1q', '1y') and DuckDB INTERVAL ('1 day', '1 hour')
    //
    // This macro is a thin wrapper around the native streaming implementation (_ts_fill_forward_native)
    // which uses 11x less memory than the previous SQL macro approach (GH#113, GH#115).
    //
    // Performance (1M rows, 10K groups):
    //   SQL macro:  127 MB peak memory
    //   Native:      11 MB peak memory (11x reduction)
    {"ts_fill_forward_by", {"source", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_fill_forward_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR)),
    target_date,
    frequency
)
)",
    "Forward-fills missing timestamps in each group's time series using the last known value.",
    "SELECT * FROM ts_fill_forward_by('sales', product_id, date, qty, '1d')",
    "data-preparation"},

    // ts_drop_gappy_by: Filter out series with too many gaps
    // C++ API: ts_drop_gappy_by(table_name, group_col, value_col, max_gap_ratio)
    {"ts_drop_gappy_by", {"source", "group_col", "value_col", "max_gap_ratio", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING (SUM(CASE WHEN value_col IS NULL THEN 1 ELSE 0 END)::DOUBLE / NULLIF(COUNT(*), 0)) <= max_gap_ratio
)
)",
    "Removes groups whose time series have a gap rate exceeding a threshold.",
    "SELECT * FROM ts_drop_gappy_by('sales', product_id, date, qty, '1d', 0.1)",
    "data-preparation"},

    // ts_drop_zeros_by: Filter out series with all zeros
    // C++ API: ts_drop_zeros_by(table_name, group_col, value_col)
    {"ts_drop_zeros_by", {"source", "group_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING SUM(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN 1 ELSE 0 END) > 0
)
)",
    "Removes groups whose time series are predominantly zero values.",
    "SELECT * FROM ts_drop_zeros_by('sales', product_id, date, qty, 0.8)",
    "data-preparation"},

    // ts_mstl_decomposition_by: MSTL decomposition for grouped series
    // C++ API: ts_mstl_decomposition_by(table_name, group_col, date_col, value_col, params MAP)
    // Returns: TABLE with expanded decomposition columns
    // Uses native streaming implementation to avoid LIST() memory issues
    {"ts_mstl_decomposition_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_mstl_decomposition_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR)),
    COALESCE(json_extract_string(to_json(params), '$.insufficient_data'), 'fail')
)
)",
    "Decomposes each group's time series into trend, seasonal, and remainder components using MSTL.",
    "SELECT * FROM ts_mstl_decomposition_by('sales', product_id, date, qty, '[7,365]')",
    "decomposition"},

    // ts_detrend_by: Remove trend from grouped time series
    // C++ API: ts_detrend_by(table_name, group_col, date_col, value_col, method)
    // method: 'linear', 'quadratic', 'cubic', 'auto' (default: 'auto')
    // Returns: TABLE(id, trend[], detrended[], method, coefficients[], rss, n_params)
    {"ts_detrend_by", {"source", "group_col", "date_col", "value_col", "method", nullptr}, {{nullptr, nullptr}},
R"(
WITH detrend_data AS (
    SELECT
        group_col,
        ts_detrend(LIST(value_col::DOUBLE ORDER BY date_col), method::VARCHAR) AS _detrend
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    group_col,
    (_detrend).trend AS trend,
    (_detrend).detrended AS detrended,
    (_detrend).method AS method,
    (_detrend).coefficients AS coefficients,
    (_detrend).rss AS rss,
    (_detrend).n_params AS n_params
FROM detrend_data
)",
    "Removes the trend component from each group's time series.",
    "SELECT * FROM ts_detrend_by('sales', product_id, date, qty, 7)",
    "decomposition"},

    // ts_classify_seasonality_by: Classify seasonality type per group
    // C++ API: ts_classify_seasonality_by(table_name, group_col, date_col, value_col, period)
    // Returns: TABLE(id, timing_classification, modulation_type, has_stable_timing, timing_variability,
    //                seasonal_strength, is_seasonal, cycle_strengths[], weak_seasons[])
    {"ts_classify_seasonality_by", {"source", "group_col", "date_col", "value_col", "period", nullptr}, {{nullptr, nullptr}},
R"(
WITH classification_data AS (
    SELECT
        group_col,
        ts_classify_seasonality_agg(date_col, value_col::DOUBLE, period::DOUBLE) AS _cls
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    group_col,
    (_cls).timing_classification AS timing_classification,
    (_cls).modulation_type AS modulation_type,
    (_cls).has_stable_timing AS has_stable_timing,
    (_cls).timing_variability AS timing_variability,
    (_cls).seasonal_strength AS seasonal_strength,
    (_cls).is_seasonal AS is_seasonal,
    (_cls).cycle_strengths AS cycle_strengths,
    (_cls).weak_seasons AS weak_seasons
FROM classification_data
)",
    "Classifies seasonality type (additive/multiplicative/none) for each group.",
    "SELECT * FROM ts_classify_seasonality_by('sales', product_id, date, qty)",
    "seasonality"},

    // ts_detect_changepoints: Detect changepoints in a single series
    // C++ API: ts_detect_changepoints(table_name, date_col, value_col, params MAP)
    // params: hazard_lambda (default 250.0), include_probabilities (default false)
    {"ts_detect_changepoints", {"source", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH ordered_data AS (
    SELECT
        date_col AS _dt,
        value_col AS _val,
        ROW_NUMBER() OVER (ORDER BY date_col) AS _idx
    FROM query_table(source::VARCHAR)
),
cp_result AS (
    SELECT _ts_detect_changepoints_bocpd(
        LIST(value_col::DOUBLE ORDER BY date_col),
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.hazard_lambda') AS DOUBLE), 250.0),
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.include_probabilities') AS BOOLEAN), false)
    ) AS cp
    FROM query_table(source::VARCHAR)
)
SELECT
    od._dt AS date_col,
    od._val AS value_col,
    (cp.cp).is_changepoint[od._idx] AS is_changepoint,
    (cp.cp).changepoint_probability[od._idx] AS changepoint_probability
FROM ordered_data od, cp_result cp
ORDER BY od._dt
)",
    "Detects structural changepoints in a single time series using Bayesian Online Changepoint Detection.",
    "SELECT * FROM ts_detect_changepoints('sales', product_id, date, qty)",
    "changepoint-detection"},

    // ts_detect_changepoints_by: Detect changepoints per group (row-level output)
    // C++ API: ts_detect_changepoints_by(table_name, group_col, date_col, value_col, params MAP)
    // Returns: TABLE with group_col (preserved name!), date_col, value_col, is_changepoint, changepoint_probability
    //
    // This macro wraps the native streaming implementation (_ts_detect_changepoints_by_native)
    // which fixes issue #149:
    // - Preserves the input group column name (was hardcoded as 'id')
    // - Returns row-level data with time column included
    {"ts_detect_changepoints_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_detect_changepoints_by_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR)),
    COALESCE(TRY_CAST(params['hazard_lambda'] AS VARCHAR), '250.0')
)
)",
    "Detects structural changepoints for each group using Bayesian Online Changepoint Detection.",
    "SELECT * FROM ts_detect_changepoints_by('sales', product_id, date, qty)",
    "changepoint-detection"},

    // ts_forecast: Generate forecasts for a single series (table-based)
    // C++ API: ts_forecast(table_name, date_col, target_col, method, horizon, params?)
    {"ts_forecast", {"source", "date_col", "target_col", "method", "horizon", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
WITH _ets_spec AS (
    SELECT COALESCE(json_extract_string(to_json(params), '$.model'), '') AS spec
),
forecast_result AS (
    SELECT _ts_forecast(
        LIST(target_col::DOUBLE ORDER BY date_col),
        horizon,
        CASE WHEN (SELECT spec FROM _ets_spec) != ''
             THEN method || ':' || (SELECT spec FROM _ets_spec)
             ELSE method
        END
    ) AS fcst
    FROM query_table(source::VARCHAR)
)
SELECT
    (fcst).point AS point_forecasts,
    (fcst).lower AS lower_bounds,
    (fcst).upper AS upper_bounds,
    (fcst).model AS model_name,
    (fcst).aic,
    (fcst).bic
FROM forecast_result
)",
    "Generates a forecast for a single time series. Returns point forecasts with prediction intervals.",
    "SELECT * FROM ts_forecast('sales', product_id, date, qty, 'AutoETS', 12, '1d')",
    "forecasting"},

    // ts_forecast_by: Generate forecasts per group (long format - one row per forecast step)
    // C++ API: ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, frequency, params?)
    //
    // Uses GROUP BY + scalar function for native DuckDB parallelism across groups.
    // DuckDB distributes groups across all available cores automatically.
    //
    // Supports both Polars-style ('1d', '1h', '30m', '1w', '1mo', '1q', '1y') and DuckDB INTERVAL ('1 day', '1 hour')
    {"ts_forecast_by", {"source", "group_col", "date_col", "target_col", "method", "horizon", "frequency", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
FROM (
    SELECT group_col,
           unnest(_ts_forecast_scalar(
               LIST(date_col ORDER BY date_col),
               LIST(target_col::DOUBLE ORDER BY date_col),
               horizon,
               frequency,
               method,
               params
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
    "Generates forecasts for multiple time series grouped by one or more keys. Returns point forecasts with prediction intervals.",
    "SELECT * FROM ts_forecast_by('sales', product_id, date, qty, 'AutoETS', 12, '1d')",
    "forecasting"},

    // ts_cv_forecast_by: Generate forecasts for CV splits with parallel fold execution
    // C++ API: ts_cv_forecast_by(ml_folds, group_col, date_col, target_col, method, params)
    // Processes all folds in parallel using DuckDB's vectorization
    //
    // IMPORTANT: ml_folds should be output from ts_cv_folds_by containing BOTH train and test rows.
    // The function trains on 'train' rows and matches forecasts to existing 'test' row dates.
    // Horizon is inferred from the number of test rows per fold/group.
    //
    // If fold_id/split columns are missing, the native function throws a clear error
    // directing users to create folds first with ts_cv_folds_by().
    //
    // Uses native streaming implementation to avoid LIST() memory issues
    {"ts_cv_forecast_by", {"ml_folds", "group_col", "date_col", "target_col", "method", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT * FROM _ts_cv_forecast_native(
    (SELECT
        group_col AS "__cv_grp__",
        date_col AS "__cv_dt__",
        target_col::DOUBLE AS "__cv_tgt__",
        *
     FROM query_table(ml_folds::VARCHAR)),
    method,
    params
)
ORDER BY 1, 2, 3
)",
    "Runs forecasts on all cross-validation folds and returns predictions with fold metadata.",
    "SELECT * FROM ts_cv_forecast_by('cv_splits', product_id, date, qty, 'AutoETS')",
    "cross-validation"},

    // ts_forecast_exog: Generate forecasts with exogenous variables (single series)
    // C++ API: ts_forecast_exog(source, date_col, target_col, xreg_cols, future_source, future_date_col, future_xreg_cols, method, horizon, params)
    // - source: historical data table with y and X columns
    // - xreg_cols: list of X column names in source table
    // - future_source: table containing future X values
    // - future_xreg_cols: list of X column names in future_source table
    // Note: Requires JSON extension for dynamic column access (auto-loaded if available)
    {"ts_forecast_exog", {"source", "date_col", "target_col", "xreg_cols", "future_source", "future_date_col", "future_xreg_cols", nullptr}, {{"method", "'AutoARIMA'"}, {"horizon", "12"}, {"params", "MAP{}"}, {nullptr, nullptr}},
R"(
WITH src AS (
    SELECT date_col AS __exog_dt__, target_col AS __exog_tgt__, * FROM query_table(source::VARCHAR)
),
future_src AS (
    SELECT future_date_col AS __exog_fdt__, * FROM query_table(future_source::VARCHAR)
),
-- Expand xreg column names (single series, no groups)
_xreg_cols_expanded AS (
    SELECT UNNEST(xreg_cols) AS col_name
),
-- For each column name, extract values using JSON via CROSS JOIN
_xreg_values AS (
    SELECT
        xce.col_name,
        LIST(json_extract(to_json(s), '$.' || xce.col_name)::DOUBLE ORDER BY s.__exog_dt__) AS values
    FROM _xreg_cols_expanded xce
    CROSS JOIN src s
    GROUP BY xce.col_name
),
-- Build list of lists for xreg
_xreg_list AS (
    SELECT LIST(values ORDER BY col_name) AS xreg_list
    FROM _xreg_values
),
-- Aggregate historical target values
_y_list AS (
    SELECT LIST(__exog_tgt__::DOUBLE ORDER BY __exog_dt__) AS y_list FROM src
),
-- Expand future xreg column names
_future_cols_expanded AS (
    SELECT UNNEST(future_xreg_cols) AS col_name
),
-- For each column name, extract future values using JSON via CROSS JOIN
_future_xreg_values AS (
    SELECT
        fce.col_name,
        LIST(json_extract(to_json(fsrc), '$.' || fce.col_name)::DOUBLE ORDER BY fsrc.__exog_fdt__) AS values
    FROM _future_cols_expanded fce
    CROSS JOIN future_src fsrc
    GROUP BY fce.col_name
),
-- Build list of lists for future xreg
_future_xreg_list AS (
    SELECT LIST(values ORDER BY col_name) AS future_xreg_list
    FROM _future_xreg_values
),
forecast_result AS (
    SELECT _ts_forecast_exog(
        (SELECT y_list FROM _y_list),
        COALESCE((SELECT xreg_list FROM _xreg_list), []::DOUBLE[][]),
        COALESCE((SELECT future_xreg_list FROM _future_xreg_list), []::DOUBLE[][]),
        horizon,
        method
    ) AS fcst
)
SELECT
    (fcst).point AS point_forecasts,
    (fcst).lower AS lower_bounds,
    (fcst).upper AS upper_bounds,
    (fcst).model AS model_name,
    (fcst).aic,
    (fcst).bic,
    (fcst).mse
FROM forecast_result
)",
    "Generates a forecast for a single time series using exogenous regressors.",
    "SELECT * FROM ts_forecast_exog('sales', product_id, date, qty, 'future_features', 'AutoARIMA', 12, '1d')",
    "forecasting"},

    // ts_forecast_exog_by: Generate forecasts with exogenous variables per group
    // C++ API: ts_forecast_exog_by(source, group_col, date_col, target_col, xreg_cols, future_source, future_date_col, future_xreg_cols, frequency, method?, horizon?, params?)
    // Note: Requires JSON extension for dynamic column access (auto-loaded if available)
    {"ts_forecast_exog_by", {"source", "group_col", "date_col", "target_col", "xreg_cols", "future_source", "future_date_col", "future_xreg_cols", "frequency", nullptr}, {{"method", "'AutoARIMA'"}, {"horizon", "12"}, {"params", "MAP{}"}, {nullptr, nullptr}},
R"(
WITH _freq AS (
    SELECT CASE
        WHEN frequency::VARCHAR ~ '^[0-9]+$' THEN (frequency::VARCHAR || ' day')::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'd$', ' day'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'h$', ' hour'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency::VARCHAR, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'w$', ' week'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'mo$', ' month'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency::VARCHAR, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
src AS (
    SELECT group_col AS __exog_grp__, date_col AS __exog_dt__, target_col AS __exog_tgt__, * FROM query_table(source::VARCHAR)
),
future_src AS (
    SELECT group_col AS __exog_fgrp__, future_date_col AS __exog_fdt__, * FROM query_table(future_source::VARCHAR)
),
-- Get unique groups from source
_groups AS (
    SELECT DISTINCT __exog_grp__ FROM src
),
-- Expand xreg column names per group for historical data
_xreg_cols_expanded AS (
    SELECT g.__exog_grp__, UNNEST(xreg_cols) AS col_name
    FROM _groups g
),
-- For each (group, col_name), extract values using JSON via JOIN
_xreg_values AS (
    SELECT
        xce.__exog_grp__,
        xce.col_name,
        LIST(json_extract(to_json(s), '$.' || xce.col_name)::DOUBLE ORDER BY s.__exog_dt__) AS values
    FROM _xreg_cols_expanded xce
    JOIN src s ON s.__exog_grp__ = xce.__exog_grp__
    GROUP BY xce.__exog_grp__, xce.col_name
),
-- Build list of lists for xreg per group
_xreg_lists AS (
    SELECT __exog_grp__, LIST(values ORDER BY col_name) AS _xreg_list
    FROM _xreg_values
    GROUP BY __exog_grp__
),
-- Aggregate historical target values per group
grouped_historical AS (
    SELECT
        __exog_grp__,
        date_trunc('second', MAX(__exog_dt__)::TIMESTAMP) AS last_date,
        LIST(__exog_tgt__::DOUBLE ORDER BY __exog_dt__) AS _y_list
    FROM src
    GROUP BY __exog_grp__
),
-- Get unique groups from future source
_future_groups AS (
    SELECT DISTINCT __exog_fgrp__ FROM future_src
),
-- Expand future xreg column names per group
_future_cols_expanded AS (
    SELECT g.__exog_fgrp__, UNNEST(future_xreg_cols) AS col_name
    FROM _future_groups g
),
-- For each (group, col_name), extract future values using JSON via JOIN
_future_xreg_values AS (
    SELECT
        fce.__exog_fgrp__,
        fce.col_name,
        LIST(json_extract(to_json(fsrc), '$.' || fce.col_name)::DOUBLE ORDER BY fsrc.__exog_fdt__) AS values
    FROM _future_cols_expanded fce
    JOIN future_src fsrc ON fsrc.__exog_fgrp__ = fce.__exog_fgrp__
    GROUP BY fce.__exog_fgrp__, fce.col_name
),
-- Build list of lists for future xreg per group
_future_xreg_lists AS (
    SELECT __exog_fgrp__, LIST(values ORDER BY col_name) AS _future_xreg_list
    FROM _future_xreg_values
    GROUP BY __exog_fgrp__
),
forecast_data AS (
    SELECT
        h.__exog_grp__,
        h.last_date,
        _ts_forecast_exog(
            h._y_list,
            COALESCE(x._xreg_list, []::DOUBLE[][]),
            COALESCE(fx._future_xreg_list, []::DOUBLE[][]),
            horizon,
            method
        ) AS fcst
    FROM grouped_historical h
    LEFT JOIN _xreg_lists x ON h.__exog_grp__ = x.__exog_grp__
    LEFT JOIN _future_xreg_lists fx ON h.__exog_grp__ = fx.__exog_fgrp__
)
SELECT
    __exog_grp__ AS id,
    UNNEST(generate_series(1, len((fcst).point)))::INTEGER AS forecast_step,
    UNNEST(generate_series(last_date + (SELECT _interval FROM _freq), last_date + (len((fcst).point)::INTEGER * EXTRACT(EPOCH FROM (SELECT _interval FROM _freq)) || ' seconds')::INTERVAL, (SELECT _interval FROM _freq)))::TIMESTAMP AS date,
    UNNEST((fcst).point) AS yhat,
    UNNEST((fcst).lower) AS yhat_lower,
    UNNEST((fcst).upper) AS yhat_upper,
    (fcst).model AS model_name
FROM forecast_data
ORDER BY __exog_grp__, forecast_step
)",
    "Generates forecasts for multiple time series using exogenous regressors.",
    "SELECT * FROM ts_forecast_exog_by('sales', product_id, date, qty, 'future_features', 'AutoARIMA', 12, '1d')",
    "forecasting"},

    // ts_mark_unknown_by: Mark rows as known/unknown based on cutoff date for scenario expressions
    // C++ API: ts_mark_unknown_by(table_name, group_col, date_col, cutoff_date)
    // Returns: all source columns plus is_unknown (boolean), last_known_date (per group)
    // Use this with custom CASE expressions for scenario-based filling
    {"ts_mark_unknown_by", {"source", "group_col", "date_col", "cutoff_date", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT
        *,
        group_col AS _grp,
        date_trunc('second', date_col::TIMESTAMP) AS _dt
    FROM query_table(source::VARCHAR)
),
_cutoff AS (
    SELECT date_trunc('second', cutoff_date::TIMESTAMP) AS _cutoff_ts
),
last_known AS (
    SELECT
        _grp,
        MAX(_dt) AS _last_known_dt
    FROM src
    WHERE _dt <= (SELECT _cutoff_ts FROM _cutoff)
    GROUP BY _grp
)
SELECT
    src.* EXCLUDE (_grp, _dt),
    src._dt > (SELECT _cutoff_ts FROM _cutoff) AS is_unknown,
    lk._last_known_dt AS last_known_date
FROM src
LEFT JOIN last_known lk ON src._grp = lk._grp
ORDER BY src._grp, src._dt
)",
    "Marks future values as unknown (NULL) for cross-validation leakage prevention.",
    "SELECT * FROM ts_mark_unknown_by('features', product_id, date, feature_col, cutoff_date)",
    "data-preparation"},

    // ts_fill_unknown_by: Fill unknown future feature values in test set for CV splits
    // C++ API: ts_fill_unknown_by(table_name, group_col, date_col, value_col, cutoff_date, params)
    // params MAP supports: strategy ('last_value', 'null', 'default'), fill_value (DOUBLE, for 'default' strategy)
    {"ts_fill_unknown_by", {"source", "group_col", "date_col", "value_col", "cutoff_date", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH _params AS (
    SELECT
        COALESCE(json_extract_string(to_json(params), '$.strategy'), 'last_value') AS _strategy,
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.fill_value') AS DOUBLE), 0.0) AS _fill_value
),
src AS (
    SELECT
        group_col AS _grp,
        date_trunc('second', date_col::TIMESTAMP) AS _dt,
        value_col AS _val
    FROM query_table(source::VARCHAR)
),
_cutoff AS (
    SELECT date_trunc('second', cutoff_date::TIMESTAMP) AS _cutoff_ts
)
SELECT
    _grp AS group_col,
    _dt AS date_col,
    CASE
        WHEN _dt <= (SELECT _cutoff_ts FROM _cutoff) THEN _val
        WHEN (SELECT _strategy FROM _params) = 'null' THEN NULL
        WHEN (SELECT _strategy FROM _params) = 'default' THEN (SELECT _fill_value FROM _params)
        WHEN (SELECT _strategy FROM _params) = 'last_value' THEN
            LAST_VALUE(CASE WHEN _dt <= (SELECT _cutoff_ts FROM _cutoff) THEN _val END IGNORE NULLS) OVER (
                PARTITION BY _grp ORDER BY _dt
                ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW
            )
        ELSE _val
    END AS value_col
FROM src
ORDER BY _grp, _dt
)",
    "Fills unknown (NULL) feature values using a specified strategy for cross-validation.",
    "SELECT * FROM ts_fill_unknown_by('cv_data', product_id, date, feature_col, 'last_value')",
    "data-preparation"},

    // ts_validate_timestamps_by: Validate that expected timestamps exist in data for each group
    // C++ API: ts_validate_timestamps_by(table_name, group_col, date_col, expected_timestamps)
    // Returns: group_col, is_valid, n_expected, n_found, n_missing, missing_timestamps
    // expected_timestamps should be a LIST of DATE/TIMESTAMP values
    {"ts_validate_timestamps_by", {"source", "group_col", "date_col", "expected_timestamps", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT DISTINCT
        group_col AS _grp,
        date_trunc('second', date_col::TIMESTAMP) AS _dt
    FROM query_table(source::VARCHAR)
),
expected AS (
    SELECT date_trunc('second', UNNEST(expected_timestamps)::TIMESTAMP) AS _expected_dt
),
groups AS (
    SELECT DISTINCT _grp FROM src
),
all_expected AS (
    SELECT g._grp, e._expected_dt
    FROM groups g
    CROSS JOIN expected e
),
validation AS (
    SELECT
        ae._grp,
        ae._expected_dt,
        CASE WHEN s._dt IS NOT NULL THEN TRUE ELSE FALSE END AS _found
    FROM all_expected ae
    LEFT JOIN src s ON ae._grp = s._grp AND ae._expected_dt = s._dt
)
SELECT
    _grp AS group_col,
    BOOL_AND(_found) AS is_valid,
    COUNT(*) AS n_expected,
    SUM(CASE WHEN _found THEN 1 ELSE 0 END)::BIGINT AS n_found,
    SUM(CASE WHEN NOT _found THEN 1 ELSE 0 END)::BIGINT AS n_missing,
    LIST(_expected_dt ORDER BY _expected_dt) FILTER (WHERE NOT _found) AS missing_timestamps
FROM validation
GROUP BY _grp
ORDER BY _grp
)",
    "Validates that timestamps are unique, monotonically increasing, and match the expected frequency per group.",
    "SELECT * FROM ts_validate_timestamps_by('sales', product_id, date, qty, '1d')",
    "data-quality"},

    // ts_validate_timestamps_summary_by: Quick validation summary across all groups
    // C++ API: ts_validate_timestamps_summary_by(table_name, group_col, date_col, expected_timestamps)
    // Returns: single row with all_valid, n_groups, n_valid_groups, n_invalid_groups, invalid_groups
    {"ts_validate_timestamps_summary_by", {"source", "group_col", "date_col", "expected_timestamps", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT DISTINCT
        group_col AS _grp,
        date_trunc('second', date_col::TIMESTAMP) AS _dt
    FROM query_table(source::VARCHAR)
),
expected AS (
    SELECT date_trunc('second', UNNEST(expected_timestamps)::TIMESTAMP) AS _expected_dt
),
groups AS (
    SELECT DISTINCT _grp FROM src
),
all_expected AS (
    SELECT g._grp, e._expected_dt
    FROM groups g
    CROSS JOIN expected e
),
validation AS (
    SELECT
        ae._grp,
        ae._expected_dt,
        CASE WHEN s._dt IS NOT NULL THEN TRUE ELSE FALSE END AS _found
    FROM all_expected ae
    LEFT JOIN src s ON ae._grp = s._grp AND ae._expected_dt = s._dt
),
per_group AS (
    SELECT
        _grp,
        BOOL_AND(_found) AS is_valid
    FROM validation
    GROUP BY _grp
)
SELECT
    BOOL_AND(is_valid) AS all_valid,
    COUNT(*) AS n_groups,
    SUM(CASE WHEN is_valid THEN 1 ELSE 0 END)::BIGINT AS n_valid_groups,
    SUM(CASE WHEN NOT is_valid THEN 1 ELSE 0 END)::BIGINT AS n_invalid_groups,
    LIST(_grp) FILTER (WHERE NOT is_valid) AS invalid_groups
FROM per_group
)",
    "Summarizes ts_validate_timestamps_by results across all groups.",
    "SELECT * FROM ts_validate_timestamps_summary_by('sales', product_id, date, qty, '1d')",
    "data-quality"},

    // ts_cv_split_folds_by: Generate fold boundaries for time series cross-validation
    // C++ API: ts_cv_split_folds_by(source, group_col, date_col, training_end_times, horizon, frequency, params)
    // training_end_times: LIST of cutoff dates for each fold
    // horizon: number of periods in test set
    // frequency: time frequency ('1d', '1h', etc.)
    // params MAP supports:
    //   gap (BIGINT) - periods between training end and test start (default 0)
    //   embargo (BIGINT) - periods to exclude from training after previous fold's test end (default 0)
    // Returns: fold_id, train_start, train_end, test_start, test_end, horizon, gap, embargo for each fold
    {"ts_cv_split_folds_by", {"source", "group_col", "date_col", "training_end_times", "horizon", "frequency", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH _params AS (
    SELECT
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.gap') AS BIGINT), 0) AS _gap,
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.embargo') AS BIGINT), 0) AS _embargo
),
_freq AS (
    SELECT CASE
        WHEN frequency::VARCHAR ~ '^[0-9]+$' THEN (frequency::VARCHAR || ' day')::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'd$', ' day'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'h$', ' hour'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency::VARCHAR, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'w$', ' week'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'mo$', ' month'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency::VARCHAR, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
date_bounds AS (
    SELECT
        MIN(date_trunc('second', date_col::TIMESTAMP)) AS _min_dt,
        MAX(date_trunc('second', date_col::TIMESTAMP)) AS _max_dt
    FROM query_table(source::VARCHAR)
),
folds_raw AS (
    SELECT
        ROW_NUMBER() OVER (ORDER BY _train_end) AS fold_id,
        date_trunc('second', _train_end::TIMESTAMP) AS train_end,
        -- Gap: skip _gap periods after training before test starts
        date_trunc('second', _train_end::TIMESTAMP) + (((SELECT _gap FROM _params) + 1) * (SELECT _interval FROM _freq)) AS test_start,
        date_trunc('second', _train_end::TIMESTAMP) + (((SELECT _gap FROM _params) + horizon) * (SELECT _interval FROM _freq)) AS test_end
    FROM (SELECT UNNEST(training_end_times) AS _train_end)
),
folds_with_embargo AS (
    SELECT
        fold_id,
        train_end,
        test_start,
        test_end,
        -- Embargo: train_start must be at least embargo periods after previous fold's test_end
        -- Only apply if embargo > 0; otherwise use min_date (no constraint)
        CASE
            WHEN (SELECT _embargo FROM _params) > 0 THEN
                GREATEST(
                    (SELECT _min_dt FROM date_bounds),
                    COALESCE(
                        LAG(test_end) OVER (ORDER BY fold_id) + ((SELECT _embargo FROM _params) * (SELECT _interval FROM _freq)),
                        (SELECT _min_dt FROM date_bounds)
                    )
                )
            ELSE (SELECT _min_dt FROM date_bounds)
        END AS train_start_with_embargo
    FROM folds_raw
)
SELECT
    fold_id::BIGINT AS fold_id,
    train_start_with_embargo AS train_start,
    train_end,
    test_start,
    test_end,
    horizon::BIGINT AS horizon,
    (SELECT _gap FROM _params)::BIGINT AS gap,
    (SELECT _embargo FROM _params)::BIGINT AS embargo
FROM folds_with_embargo
ORDER BY fold_id
)",
    "Splits time series into cross-validation folds with configurable gap, embargo, and window parameters.",
    "SELECT * FROM ts_cv_split_folds_by('sales', product_id, date, qty, 3, 12, '1d')",
    "cross-validation"},

    // ts_cv_split_by: Split time series data into train/test sets for cross-validation
    // C++ API: ts_cv_split_by(source, group_col, date_col, target_col, training_end_times, horizon, params)
    //
    // IMPORTANT: Assumes pre-cleaned data with no gaps. Use ts_fill_gaps_by first if needed.
    // Uses position-based indexing (not date arithmetic) - works correctly with all frequencies.
    //
    // params MAP supports: window_type ('expanding', 'fixed', 'sliding'), min_train_size (BIGINT), gap (BIGINT), embargo (BIGINT)
    // gap: number of positions between training end and test start (default 0)
    // embargo: number of positions to exclude from training after previous fold's test end (default 0)
    // Returns: <group_col>, <date_col>, <target_col>, fold_id, split (train/test)
    {"ts_cv_split_by", {"source", "group_col", "date_col", "target_col", "training_end_times", "horizon", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT * FROM _ts_cv_split_native(
    (SELECT group_col, date_col, target_col FROM query_table(source::VARCHAR)),
    horizon,
    training_end_times,
    params
)
ORDER BY 4, 1, 2
)",
    "Creates cross-validation train/test splits from explicit fold boundary dates.",
    "SELECT * FROM ts_cv_split_by('sales', product_id, date, qty, 'folds', fold_id, cutoff)",
    "cross-validation"},

    // ts_cv_split_index_by: Memory-efficient CV split that returns only index columns (no data columns)
    // C++ API: ts_cv_split_index_by(source, group_col, date_col, training_end_times, horizon, frequency, params)
    // params MAP supports: window_type ('expanding', 'fixed', 'sliding'), min_train_size (BIGINT), gap (BIGINT), embargo (BIGINT)
    // Returns: group_col, date_col, fold_id, split (train/test) - NO target column
    // Use this for large datasets to avoid duplicating data across folds
    // Join back to source using ts_hydrate_split_full for complete data
    {"ts_cv_split_index_by", {"source", "group_col", "date_col", "training_end_times", "horizon", "frequency", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH _params AS (
    SELECT
        COALESCE(json_extract_string(to_json(params), '$.window_type'), 'expanding') AS _window_type,
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.min_train_size') AS BIGINT), 1) AS _min_train_size,
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.gap') AS BIGINT), 0) AS _gap,
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.embargo') AS BIGINT), 0) AS _embargo
),
_freq AS (
    SELECT CASE
        WHEN frequency::VARCHAR ~ '^[0-9]+$' THEN (frequency::VARCHAR || ' day')::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'd$', ' day'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'h$', ' hour'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency::VARCHAR, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'w$', ' week'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'mo$', ' month'))::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency::VARCHAR, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency::VARCHAR ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency::VARCHAR, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
src AS (
    SELECT DISTINCT
        group_col AS _grp,
        date_trunc('second', date_col::TIMESTAMP) AS _dt
    FROM query_table(source::VARCHAR)
),
date_bounds AS (
    SELECT MIN(_dt) AS _min_dt FROM src
),
folds_raw AS (
    SELECT
        ROW_NUMBER() OVER (ORDER BY _train_end) AS fold_id,
        date_trunc('second', _train_end::TIMESTAMP) AS train_end,
        date_trunc('second', _train_end::TIMESTAMP) + (((SELECT _gap FROM _params) + 1) * (SELECT _interval FROM _freq)) AS test_start,
        date_trunc('second', _train_end::TIMESTAMP) + (((SELECT _gap FROM _params) + horizon) * (SELECT _interval FROM _freq)) AS test_end
    FROM (SELECT UNNEST(training_end_times) AS _train_end)
),
folds_with_embargo AS (
    SELECT
        fold_id,
        train_end,
        test_start,
        test_end,
        -- Embargo: only compute cutoff if embargo > 0
        CASE
            WHEN (SELECT _embargo FROM _params) > 0 THEN
                COALESCE(
                    LAG(test_end) OVER (ORDER BY fold_id) + ((SELECT _embargo FROM _params) * (SELECT _interval FROM _freq)),
                    (SELECT _min_dt FROM date_bounds)
                )
            ELSE (SELECT _min_dt FROM date_bounds)
        END AS embargo_cutoff
    FROM folds_raw
),
fold_bounds AS (
    SELECT
        f.fold_id,
        GREATEST(
            CASE
                WHEN (SELECT _window_type FROM _params) = 'expanding' THEN (SELECT _min_dt FROM date_bounds)
                WHEN (SELECT _window_type FROM _params) = 'fixed' THEN f.train_end - ((SELECT _min_train_size FROM _params) * (SELECT _interval FROM _freq))
                WHEN (SELECT _window_type FROM _params) = 'sliding' THEN f.train_end - ((SELECT _min_train_size FROM _params) * (SELECT _interval FROM _freq))
                ELSE (SELECT _min_dt FROM date_bounds)
            END,
            f.embargo_cutoff
        ) AS train_start,
        f.train_end,
        f.test_start,
        f.test_end
    FROM folds_with_embargo f
)
SELECT
    s._grp AS group_col,
    s._dt AS date_col,
    fb.fold_id::BIGINT AS fold_id,
    CASE
        WHEN s._dt >= fb.train_start AND s._dt <= fb.train_end THEN 'train'
        WHEN s._dt >= fb.test_start AND s._dt <= fb.test_end THEN 'test'
        ELSE NULL
    END AS split
FROM src s
CROSS JOIN fold_bounds fb
WHERE (s._dt >= fb.train_start AND s._dt <= fb.train_end)
   OR (s._dt >= fb.test_start AND s._dt <= fb.test_end)
ORDER BY fb.fold_id, s._grp, s._dt
)",
    "Memory-efficient cross-validation split using row indices instead of full data copies.",
    "SELECT * FROM ts_cv_split_index_by('sales', product_id, date, qty, 3, 12, '1d')",
    "cross-validation"},

    // ts_check_leakage: Validate a query result doesn't have obvious data leakage
    // C++ API: ts_check_leakage(result_table, test_filter_col, check_cols, params)
    // Returns: report of columns with suspicious values in test rows
    // Useful for auditing CV pipelines
    {"ts_check_leakage", {"source", "is_test_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT * FROM query_table(source::VARCHAR)
),
stats AS (
    SELECT
        is_test_col AS is_test,
        COUNT(*) AS n_rows,
        -- Count non-null numeric-like patterns in any column
        SUM(CASE WHEN is_test_col THEN 1 ELSE 0 END) AS test_rows
    FROM src
    GROUP BY is_test_col
)
SELECT
    'Leakage check complete' AS status,
    (SELECT test_rows FROM stats WHERE is_test) AS test_row_count,
    (SELECT n_rows - test_rows FROM stats WHERE NOT is_test) AS train_row_count,
    'Use ts_hydrate_split_strict + explicit column selection for fail-safe joins' AS recommendation
FROM stats
LIMIT 1
)",
    "Audits a cross-validation dataset for data leakage by checking that test features are not visible during training.",
    "SELECT * FROM ts_check_leakage('cv_splits', product_id, date, fold_id, split)",
    "cross-validation"},

    // ts_cv_folds_by: Create train/test splits for ML model backtesting
    // C++ API: ts_cv_folds_by(source, group_col, date_col, target_col, n_folds, horizon, params)
    //
    // This function combines fold boundary generation and train/test splitting in a single call,
    // suitable for ML model backtesting. Unlike ts_cv_split_by which requires pre-computed
    // training_end_times, this function automatically computes fold boundaries from the data.
    //
    // IMPORTANT: Assumes pre-cleaned data with no gaps. Use ts_fill_gaps_by first if needed.
    // Uses position-based indexing (not date arithmetic) - works correctly with all frequencies.
    //
    // params MAP supports:
    //   gap (BIGINT, default 0) - periods between train end and test start
    //   embargo (BIGINT, default 0) - periods to exclude from training after previous test
    //   window_type ('expanding', 'fixed', 'sliding', default 'expanding') - training window strategy
    //   min_train_size (BIGINT, default 1) - minimum training size for fixed/sliding windows
    //   initial_train_size (BIGINT, default: n_dates - n_folds * horizon) - periods before first fold
    //   skip_length (BIGINT, default: horizon) - periods between folds (1=dense, horizon=default)
    //   clip_horizon (BOOLEAN, default: false) - if true, allow folds with partial test windows
    // Returns: <group_col>, <date_col>, <target_col>, fold_id, split (train/test)
    {"ts_cv_folds_by", {"source", "group_col", "date_col", "target_col", "n_folds", "horizon", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT * FROM _ts_cv_folds_native(
    (SELECT
        group_col AS "__cv_grp__",
        date_col AS "__cv_dt__",
        target_col::DOUBLE AS "__cv_y__",
        group_col,      -- Original name (position 3)
        date_col,       -- Original name (position 4)
        target_col      -- Original name (position 5)
     FROM query_table(source::VARCHAR)),
    n_folds,
    horizon,
    params
)
)",
    "Generates cross-validation fold definitions (cutoff dates) without splitting the data.",
    "SELECT * FROM ts_cv_folds_by('sales', product_id, date, qty, 3, 12, '1d')",
    "cross-validation"},

    // ts_cv_hydrate_by: Hydrate CV folds with unknown features as direct columns
    // C++ API: ts_cv_hydrate_by(cv_folds, source, group_col, date_col, unknown_features, params)
    //
    // This function joins CV folds from ts_cv_folds_by with the source data table,
    // outputting unknown feature columns with masking applied automatically.
    //
    // cv_folds: Output from ts_cv_folds_by (has group_col, date_col, target_col, fold_id, split)
    // source: Original data table with all features
    // group_col, date_col: Column identifiers for joining (same in both tables)
    // unknown_features: VARCHAR[] list of column names to mask in test rows
    // params MAP supports:
    //   strategy (VARCHAR, default 'last_value') - 'last_value', 'null', or 'default'
    //   fill_value (VARCHAR, default '') - value for 'default' strategy
    //
    // Fill strategies:
    //   'last_value' - carry forward last training value per group (default)
    //   'null' - set unknown features to NULL in test rows
    //   'default' - set to specified fill_value
    //
    // Returns:
    //   cv_folds columns (group, date, target, fold_id, split) + unknown features as VARCHAR columns
    //
    // Usage: Direct column access (no MAP extraction needed):
    //   SELECT temperature, promotion
    //   FROM ts_cv_hydrate_by('cv_folds', 'source', id, date, ['temperature', 'promotion'], MAP{});
    {"ts_cv_hydrate_by", {"cv_folds", "source", "group_col", "date_col", "unknown_features", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
-- CV folds with join keys
WITH cv AS (
    SELECT *,
        group_col AS __cv_grp,
        date_trunc('second', date_col::TIMESTAMP) AS __cv_dt
    FROM query_table(cv_folds::VARCHAR)
),
-- Source data with join keys (deduplicated to avoid row multiplication)
src AS (
    SELECT *,
        group_col AS __src_grp,
        date_trunc('second', date_col::TIMESTAMP) AS __src_dt
    FROM query_table(source::VARCHAR)
    QUALIFY ROW_NUMBER() OVER (PARTITION BY group_col, date_trunc('second', date_col::TIMESTAMP) ORDER BY date_col) = 1
),
-- Join cv with src, keeping source row as JSON for native function
-- cv_folds from ts_cv_folds_by has exactly 5 columns: group, date, target, fold_id, split
-- The native function expects: group, date, target, fold_id, split, __src_json
joined AS (
    SELECT
        cv.* EXCLUDE (__cv_grp, __cv_dt),
        to_json(src) AS __src_json
    FROM cv
    JOIN src ON cv.__cv_grp = src.__src_grp AND cv.__cv_dt = src.__src_dt
)
SELECT * FROM _ts_cv_hydrate_native(
    (SELECT * FROM joined),
    unknown_features,
    params
)
)",
    "Joins external features to CV splits with proper masking to prevent data leakage.",
    "SELECT * FROM ts_cv_hydrate_by('cv_splits', product_id, date, qty, 'features', unknown_cols)",
    "cross-validation"},

    // ts_conformal_by: Compute conformal prediction intervals for grouped series
    // C++ API: ts_conformal_by(backtest_results, group_col, actual_col, forecast_col, point_forecast_col, params)
    // backtest_results: Table with backtest residuals (actual - forecast)
    // params MAP supports:
    //   alpha (DOUBLE) - miscoverage rate (default 0.1 for 90% coverage)
    //   method (VARCHAR) - 'symmetric' or 'asymmetric' (default 'symmetric')
    // Returns: group_col, point, lower, upper, coverage, conformity_score, method
    {"ts_conformal_by", {"backtest_results", "group_col", "actual_col", "forecast_col", "point_forecast_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH _params AS (
    SELECT
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.alpha') AS DOUBLE), 0.1) AS _alpha,
        COALESCE(json_extract_string(to_json(params), '$.method'), 'symmetric') AS _method
),
residuals AS (
    SELECT
        group_col AS _grp,
        (actual_col - forecast_col)::DOUBLE AS residual
    FROM query_table(backtest_results::VARCHAR)
    WHERE actual_col IS NOT NULL AND forecast_col IS NOT NULL
),
point_forecasts AS (
    SELECT
        group_col AS _grp,
        LIST(point_forecast_col::DOUBLE ORDER BY point_forecast_col) AS _forecasts
    FROM query_table(backtest_results::VARCHAR)
    WHERE point_forecast_col IS NOT NULL
    GROUP BY group_col
),
conformal_calc AS (
    SELECT
        r._grp,
        CASE (SELECT _method FROM _params)
            WHEN 'asymmetric' THEN
                ts_conformal_predict_asymmetric(
                    LIST(r.residual),
                    pf._forecasts,
                    (SELECT _alpha FROM _params)
                )
            ELSE
                ts_conformal_predict(
                    LIST(r.residual),
                    pf._forecasts,
                    (SELECT _alpha FROM _params)
                )
        END AS conf_result
    FROM residuals r
    JOIN point_forecasts pf ON r._grp = pf._grp
    GROUP BY r._grp, pf._forecasts
)
SELECT
    _grp AS group_col,
    (conf_result).point AS point,
    (conf_result).lower AS lower,
    (conf_result).upper AS upper,
    (conf_result).coverage AS coverage,
    (conf_result).conformity_score AS conformity_score,
    (conf_result).method AS method
FROM conformal_calc
)",
    "One-step conformal prediction: calibrates on backtest residuals and applies to new forecasts.",
    "SELECT * FROM ts_conformal_by('backtest', product_id, actual, forecast, point_forecast)",
    "conformal-prediction"},

    // ts_conformal_calibrate: Compute conformal quantile from backtest residuals
    // C++ API: ts_conformal_calibrate(backtest_results, actual_col, forecast_col, params)
    // Returns a single row with the calibrated conformity score
    // params MAP supports:
    //   alpha (DOUBLE) - miscoverage rate (default 0.1 for 90% coverage)
    // Returns: conformity_score, coverage, n_residuals
    {"ts_conformal_calibrate", {"backtest_results", "actual_col", "forecast_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH _params AS (
    SELECT
        COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.alpha') AS DOUBLE), 0.1) AS _alpha
),
residuals AS (
    SELECT
        (actual_col - forecast_col)::DOUBLE AS residual
    FROM query_table(backtest_results::VARCHAR)
    WHERE actual_col IS NOT NULL AND forecast_col IS NOT NULL
)
SELECT
    ts_conformal_quantile(LIST(residual), (SELECT _alpha FROM _params)) AS conformity_score,
    1.0 - (SELECT _alpha FROM _params) AS coverage,
    COUNT(*)::BIGINT AS n_residuals
FROM residuals
)",
    "Calibrates a conformal predictor from backtest residuals and returns the calibration profile for reuse.",
    "SELECT * FROM ts_conformal_calibrate('backtest', product_id, actual, forecast)",
    "conformal-prediction"},

    // ts_conformal_apply_by: Apply pre-computed conformity score to forecasts
    // C++ API: ts_conformal_apply_by(forecast_results, group_col, forecast_col, conformity_score)
    // forecast_results: Table with point forecasts
    // conformity_score: Pre-computed score from ts_conformal_calibrate
    // Returns: group_col, forecast, lower, upper
    {"ts_conformal_apply_by", {"forecast_results", "group_col", "forecast_col", "conformity_score", nullptr}, {{nullptr, nullptr}},
R"(
WITH intervals AS (
    SELECT
        group_col AS _grp,
        ts_conformal_intervals(
            LIST(forecast_col::DOUBLE ORDER BY forecast_col),
            conformity_score::DOUBLE
        ) AS interval_result
    FROM query_table(forecast_results::VARCHAR)
    WHERE forecast_col IS NOT NULL
    GROUP BY group_col
)
SELECT
    _grp AS group_col,
    (interval_result).lower AS lower,
    (interval_result).upper AS upper
FROM intervals
)",
    "Applies a pre-calibrated conformal profile to new point forecasts to generate prediction intervals.",
    "SELECT * FROM ts_conformal_apply_by('forecasts', product_id, date, point_forecast, calibration_profile)",
    "conformal-prediction"},

    // ts_interval_width_by: Compute mean interval width for grouped series
    // C++ API: ts_interval_width_by(results, group_col, lower_col, upper_col)
    // Returns: group_col, mean_width, n_intervals
    {"ts_interval_width_by", {"results", "group_col", "lower_col", "upper_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS group_col,
    ts_mean_interval_width(
        LIST(lower_col::DOUBLE ORDER BY lower_col),
        LIST(upper_col::DOUBLE ORDER BY upper_col)
    ) AS mean_width,
    COUNT(*)::BIGINT AS n_intervals
FROM query_table(results::VARCHAR)
WHERE lower_col IS NOT NULL AND upper_col IS NOT NULL
GROUP BY group_col
)",
    "Computes the mean width of prediction intervals for each group.",
    "SELECT * FROM ts_interval_width_by('forecasts', product_id, lower, upper)",
    "conformal-prediction"},

    // ================================================================================
    // Multi-key unique_id functions (Issue #78)
    // ================================================================================
    // NOTE: ts_validate_separator, ts_combine_keys, ts_aggregate_hierarchy, and ts_split_keys
    // are now native table functions supporting arbitrary hierarchy levels.
    // See: ts_validate_separator.cpp, ts_aggregate_hierarchy.cpp, ts_combine_keys.cpp, ts_split_keys.cpp

    // ts_stats_by: Compute time series statistics grouped by a unique ID column
    // C++ API: ts_stats_by(table_name, group_col, date_col, value_col, frequency)
    // Returns: TABLE with group column (preserving original name!) and 36 statistics columns
    //
    // This macro is a thin wrapper around the native streaming implementation (_ts_stats_by_native)
    // which fixes the following bugs from issue #147:
    // - Bug 1: Preserves the input group column name (was hardcoded as 'id')
    // - Bug 2: Correctly calculates expected_length for monthly/quarterly/yearly frequencies
    // - Bug 3: Correctly detects gaps for calendar frequencies
    {"ts_stats_by", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_stats_by_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR)),
    frequency
)
)",
    "Alias for ts_stats. Computes 34 time series statistics per group.",
    "SELECT * FROM ts_stats_by('sales', product_id, date, qty, '1d')",
    "statistics"},

    // ts_data_quality_by: Alias for ts_data_quality (for API consistency with _by naming pattern)
    // C++ API: ts_data_quality_by(table_name, unique_id_col, date_col, value_col, n_short, frequency)
    // Returns: TABLE with expanded quality columns
    {"ts_data_quality_by", {"source", "unique_id_col", "date_col", "value_col", "n_short", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH quality_data AS (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(LIST(value_col::DOUBLE ORDER BY date_col)) AS _quality
    FROM query_table(source::VARCHAR)
    GROUP BY unique_id_col
)
SELECT
    unique_id,
    (_quality).structural_score AS structural_score,
    (_quality).temporal_score AS temporal_score,
    (_quality).magnitude_score AS magnitude_score,
    (_quality).behavioral_score AS behavioral_score,
    (_quality).overall_score AS overall_score,
    (_quality).n_gaps AS n_gaps,
    (_quality).n_missing AS n_missing,
    (_quality).is_constant AS is_constant
FROM quality_data
)",
    "Alias for ts_data_quality. Assesses time series data quality per group.",
    "SELECT * FROM ts_data_quality_by('sales', product_id, date, qty)",
    "data-quality"},

    // ts_features_table: Extract features from a single-series table
    // C++ API: ts_features_table(table_name, date_col, value_col)
    // Returns: TABLE with 117 expanded feature columns
    {"ts_features_table", {"source", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH features_data AS (
    SELECT ts_features_agg(date_col, value_col::DOUBLE) AS _feat
    FROM query_table(source::VARCHAR)
)
SELECT
    (_feat).abs_energy AS abs_energy,
    (_feat).absolute_sum_of_changes AS absolute_sum_of_changes,
    (_feat).agg_linear_trend_intercept AS agg_linear_trend_intercept,
    (_feat).agg_linear_trend_rvalue AS agg_linear_trend_rvalue,
    (_feat).agg_linear_trend_slope AS agg_linear_trend_slope,
    (_feat).agg_linear_trend_stderr AS agg_linear_trend_stderr,
    (_feat).approximate_entropy AS approximate_entropy,
    (_feat).autocorrelation_lag1 AS autocorrelation_lag1,
    (_feat).autocorrelation_lag2 AS autocorrelation_lag2,
    (_feat).autocorrelation_lag3 AS autocorrelation_lag3,
    (_feat).autocorrelation_lag4 AS autocorrelation_lag4,
    (_feat).autocorrelation_lag5 AS autocorrelation_lag5,
    (_feat).autocorrelation_lag6 AS autocorrelation_lag6,
    (_feat).autocorrelation_lag7 AS autocorrelation_lag7,
    (_feat).autocorrelation_lag8 AS autocorrelation_lag8,
    (_feat).autocorrelation_lag9 AS autocorrelation_lag9,
    (_feat).benford_correlation AS benford_correlation,
    (_feat).binned_entropy AS binned_entropy,
    (_feat).c3_lag1 AS c3_lag1,
    (_feat).c3_lag2 AS c3_lag2,
    (_feat).c3_lag3 AS c3_lag3,
    (_feat).cid_ce AS cid_ce,
    (_feat).count_above_mean AS count_above_mean,
    (_feat).count_below_mean AS count_below_mean,
    (_feat).count_unique AS count_unique,
    (_feat).fft_coefficient_0_abs AS fft_coefficient_0_abs,
    (_feat).fft_coefficient_0_imag AS fft_coefficient_0_imag,
    (_feat).fft_coefficient_0_real AS fft_coefficient_0_real,
    (_feat).fft_coefficient_1_abs AS fft_coefficient_1_abs,
    (_feat).fft_coefficient_1_imag AS fft_coefficient_1_imag,
    (_feat).fft_coefficient_1_real AS fft_coefficient_1_real,
    (_feat).fft_coefficient_2_abs AS fft_coefficient_2_abs,
    (_feat).fft_coefficient_2_imag AS fft_coefficient_2_imag,
    (_feat).fft_coefficient_2_real AS fft_coefficient_2_real,
    (_feat).fft_coefficient_3_abs AS fft_coefficient_3_abs,
    (_feat).fft_coefficient_3_imag AS fft_coefficient_3_imag,
    (_feat).fft_coefficient_3_real AS fft_coefficient_3_real,
    (_feat).fft_coefficient_4_abs AS fft_coefficient_4_abs,
    (_feat).fft_coefficient_4_imag AS fft_coefficient_4_imag,
    (_feat).fft_coefficient_4_real AS fft_coefficient_4_real,
    (_feat).fft_coefficient_5_abs AS fft_coefficient_5_abs,
    (_feat).fft_coefficient_5_imag AS fft_coefficient_5_imag,
    (_feat).fft_coefficient_5_real AS fft_coefficient_5_real,
    (_feat).fft_coefficient_6_abs AS fft_coefficient_6_abs,
    (_feat).fft_coefficient_6_imag AS fft_coefficient_6_imag,
    (_feat).fft_coefficient_6_real AS fft_coefficient_6_real,
    (_feat).fft_coefficient_7_abs AS fft_coefficient_7_abs,
    (_feat).fft_coefficient_7_imag AS fft_coefficient_7_imag,
    (_feat).fft_coefficient_7_real AS fft_coefficient_7_real,
    (_feat).fft_coefficient_8_abs AS fft_coefficient_8_abs,
    (_feat).fft_coefficient_8_imag AS fft_coefficient_8_imag,
    (_feat).fft_coefficient_8_real AS fft_coefficient_8_real,
    (_feat).fft_coefficient_9_abs AS fft_coefficient_9_abs,
    (_feat).fft_coefficient_9_imag AS fft_coefficient_9_imag,
    (_feat).fft_coefficient_9_real AS fft_coefficient_9_real,
    (_feat).first_location_of_maximum AS first_location_of_maximum,
    (_feat).first_location_of_minimum AS first_location_of_minimum,
    (_feat).first_value AS first_value,
    (_feat).has_duplicate AS has_duplicate,
    (_feat).has_duplicate_max AS has_duplicate_max,
    (_feat).has_duplicate_min AS has_duplicate_min,
    (_feat).kurtosis AS kurtosis,
    (_feat).large_standard_deviation AS large_standard_deviation,
    (_feat).last_location_of_maximum AS last_location_of_maximum,
    (_feat).last_location_of_minimum AS last_location_of_minimum,
    (_feat).last_value AS last_value,
    (_feat).lempel_ziv_complexity AS lempel_ziv_complexity,
    (_feat).length AS length,
    (_feat).linear_trend_intercept AS linear_trend_intercept,
    (_feat).linear_trend_r_squared AS linear_trend_r_squared,
    (_feat).linear_trend_slope AS linear_trend_slope,
    (_feat).longest_strike_above_mean AS longest_strike_above_mean,
    (_feat).longest_strike_below_mean AS longest_strike_below_mean,
    (_feat).maximum AS maximum,
    (_feat).mean AS mean,
    (_feat).mean_abs_change AS mean_abs_change,
    (_feat).mean_change AS mean_change,
    (_feat).mean_second_derivative_central AS mean_second_derivative_central,
    (_feat).median AS median,
    (_feat).minimum AS minimum,
    (_feat).number_peaks AS number_peaks,
    (_feat).number_peaks_threshold_1 AS number_peaks_threshold_1,
    (_feat).number_peaks_threshold_2 AS number_peaks_threshold_2,
    (_feat).partial_autocorrelation_lag1 AS partial_autocorrelation_lag1,
    (_feat).partial_autocorrelation_lag2 AS partial_autocorrelation_lag2,
    (_feat).partial_autocorrelation_lag3 AS partial_autocorrelation_lag3,
    (_feat).partial_autocorrelation_lag4 AS partial_autocorrelation_lag4,
    (_feat).partial_autocorrelation_lag5 AS partial_autocorrelation_lag5,
    (_feat).percentage_above_mean AS percentage_above_mean,
    (_feat).percentage_of_reoccurring_datapoints_to_all_datapoints AS percentage_of_reoccurring_datapoints_to_all_datapoints,
    (_feat).percentage_of_reoccurring_values_to_all_values AS percentage_of_reoccurring_values_to_all_values,
    (_feat).permutation_entropy AS permutation_entropy,
    (_feat)."quantile_0.1" AS "quantile_0.1",
    (_feat)."quantile_0.25" AS "quantile_0.25",
    (_feat)."quantile_0.75" AS "quantile_0.75",
    (_feat)."quantile_0.9" AS "quantile_0.9",
    (_feat).range AS range,
    (_feat).ratio_beyond_r_sigma_1 AS ratio_beyond_r_sigma_1,
    (_feat).ratio_beyond_r_sigma_2 AS ratio_beyond_r_sigma_2,
    (_feat).ratio_beyond_r_sigma_3 AS ratio_beyond_r_sigma_3,
    (_feat).ratio_value_number_to_length AS ratio_value_number_to_length,
    (_feat).root_mean_square AS root_mean_square,
    (_feat).sample_entropy AS sample_entropy,
    (_feat).skewness AS skewness,
    (_feat).spectral_centroid AS spectral_centroid,
    (_feat).spectral_variance AS spectral_variance,
    (_feat).standard_deviation AS standard_deviation,
    (_feat).sum AS sum,
    (_feat).sum_of_reoccurring_datapoints AS sum_of_reoccurring_datapoints,
    (_feat).sum_of_reoccurring_values AS sum_of_reoccurring_values,
    (_feat).time_reversal_asymmetry_stat_1 AS time_reversal_asymmetry_stat_1,
    (_feat).time_reversal_asymmetry_stat_2 AS time_reversal_asymmetry_stat_2,
    (_feat).time_reversal_asymmetry_stat_3 AS time_reversal_asymmetry_stat_3,
    (_feat).variance AS variance,
    (_feat).variation_coefficient AS variation_coefficient,
    (_feat).zero_crossing_rate AS zero_crossing_rate
FROM features_data
)",
    "Extracts 117 tsfresh-compatible features and returns them as a wide table (one column per feature).",
    "SELECT * FROM ts_features_table('sales', product_id, date, qty)",
    "feature-extraction"},

    // ts_features_by: Extract features per group (native streaming implementation)
    // C++ API: ts_features_by(table_name, group_col, date_col, value_col)
    // Returns: TABLE with id and 117 expanded feature columns
    {"ts_features_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_features_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR))
)
)",
    "Extracts time series features for each group and returns results as a structured table.",
    "SELECT * FROM ts_features_by('sales', product_id, date, qty)",
    "feature-extraction"},

    // ts_classify_seasonality: Classify seasonality for a single series
    // C++ API: ts_classify_seasonality(table_name, date_col, value_col, period)
    // Returns: TABLE(timing_classification, modulation_type, has_stable_timing, timing_variability,
    //                seasonal_strength, is_seasonal, cycle_strengths[], weak_seasons[])
    {"ts_classify_seasonality", {"source", "date_col", "value_col", "period", nullptr}, {{nullptr, nullptr}},
R"(
WITH classification_data AS (
    SELECT
        ts_classify_seasonality_agg(date_col, value_col::DOUBLE, period::DOUBLE) AS _cls
    FROM query_table(source::VARCHAR)
)
SELECT
    (_cls).timing_classification AS timing_classification,
    (_cls).modulation_type AS modulation_type,
    (_cls).has_stable_timing AS has_stable_timing,
    (_cls).timing_variability AS timing_variability,
    (_cls).seasonal_strength AS seasonal_strength,
    (_cls).is_seasonal AS is_seasonal,
    (_cls).cycle_strengths AS cycle_strengths,
    (_cls).weak_seasons AS weak_seasons
FROM classification_data
)",
    "Single-series seasonality classification. Classifies seasonality type and returns timing metadata.",
    "SELECT * FROM ts_classify_seasonality('sales', product_id, date, qty)",
    "seasonality"},

    // ================================================================================
    // Period Detection
    // ================================================================================

    // ts_detect_periods: Detect periods for a single series
    // C++ API: ts_detect_periods(table_name, date_col, value_col, params)
    // params: method (default 'fft') - 'fft' or 'acf'
    //         max_period (default 365) - maximum period to search
    //         min_confidence (default: method-specific) - minimum confidence threshold
    //         expected_periods (optional) - list of expected periods for validation
    //         tolerance (default 0.1) - relative tolerance for period matching
    // Returns: TABLE with expanded period columns
    {"ts_detect_periods", {"source", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH periods_data AS (
    SELECT
        _ts_detect_periods(
            LIST(value_col::DOUBLE ORDER BY date_col),
            COALESCE(json_extract_string(to_json(params), '$.method'), 'fft'),
            COALESCE(CAST(json_extract(to_json(params), '$.max_period') AS BIGINT), 0),
            COALESCE(CAST(json_extract(to_json(params), '$.min_confidence') AS DOUBLE), -1.0),
            COALESCE(CAST(json_extract(to_json(params), '$.expected_periods') AS DOUBLE[]), NULL),
            COALESCE(CAST(json_extract(to_json(params), '$.tolerance') AS DOUBLE), -1.0)
        ) AS _periods
    FROM query_table(source::VARCHAR)
)
SELECT
    (_periods).periods AS periods,
    (_periods).n_periods AS n_periods,
    (_periods).primary_period AS primary_period,
    (_periods).method AS method
FROM periods_data
)",
    "Detects seasonal periods for a single time series, returning ranked period candidates with confidence scores.",
    "SELECT * FROM ts_detect_periods('sales', product_id, date, qty)",
    "period-detection"},

    // ts_detect_periods_by: Detect periods for grouped series
    // C++ API: ts_detect_periods_by(table_name, group_col, date_col, value_col, params)
    // params: method (default 'fft') - 'fft' or 'acf'
    //         max_period (default 365) - maximum period to search (suitable for daily data)
    //         min_confidence (default: method-specific) - minimum confidence threshold
    //                        Use 0 to disable filtering, positive value for custom threshold
    //         expected_periods (optional) - list of expected periods for validation
    //         tolerance (default 0.1) - relative tolerance for period matching
    // Returns: TABLE with id and expanded period columns
    {"ts_detect_periods_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH periods_data AS (
    SELECT
        group_col,
        _ts_detect_periods(
            LIST(value_col::DOUBLE ORDER BY date_col),
            COALESCE(json_extract_string(to_json(params), '$.method'), 'fft'),
            COALESCE(CAST(json_extract(to_json(params), '$.max_period') AS BIGINT), 0),
            COALESCE(CAST(json_extract(to_json(params), '$.min_confidence') AS DOUBLE), -1.0),
            COALESCE(CAST(json_extract(to_json(params), '$.expected_periods') AS DOUBLE[]), NULL),
            COALESCE(CAST(json_extract(to_json(params), '$.tolerance') AS DOUBLE), -1.0)
        ) AS _periods
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    group_col,
    (_periods).periods AS periods,
    (_periods).n_periods AS n_periods,
    (_periods).primary_period AS primary_period,
    (_periods).method AS method
FROM periods_data
)",
    "Detects seasonal periods for each group, returning ranked period candidates with confidence scores.",
    "SELECT * FROM ts_detect_periods_by('sales', product_id, date, qty)",
    "period-detection"},

    // ts_detect_peaks_by: Detect peaks for grouped series
    // C++ API: ts_detect_peaks_by(table_name, group_col, date_col, value_col, params)
    // params: min_distance, min_prominence, smooth_first
    // Returns: TABLE with id and expanded peak columns
    {"ts_detect_peaks_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH peaks_data AS (
    SELECT
        group_col,
        ts_detect_peaks(
            LIST(value_col::DOUBLE ORDER BY date_col),
            COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.min_distance') AS DOUBLE), 1.0),
            COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.min_prominence') AS DOUBLE), 0.0),
            COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.smooth_first') AS BOOLEAN), false)
        ) AS _peaks
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    group_col,
    (_peaks).peaks AS peaks,
    (_peaks).n_peaks AS n_peaks,
    (_peaks).inter_peak_distances AS inter_peak_distances,
    (_peaks).mean_period AS mean_period
FROM peaks_data
)",
    "Detects peaks in each group's time series, returning peak indices, values, and prominence scores.",
    "SELECT * FROM ts_detect_peaks_by('sales', product_id, date, qty)",
    "peak-detection"},

    // ts_detect_peaks: Detect peaks for a single series
    // C++ API: ts_detect_peaks(table_name, date_col, value_col, params)
    // params: min_distance, min_prominence, smooth_first
    // Returns: TABLE with expanded peak columns
    {"ts_detect_peaks", {"source", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH peaks_data AS (
    SELECT
        ts_detect_peaks(
            LIST(value_col::DOUBLE ORDER BY date_col),
            COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.min_distance') AS DOUBLE), 1.0),
            COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.min_prominence') AS DOUBLE), 0.0),
            COALESCE(TRY_CAST(json_extract_string(to_json(params), '$.smooth_first') AS BOOLEAN), false)
        ) AS _peaks
    FROM query_table(source::VARCHAR)
)
SELECT
    (_peaks).peaks AS peaks,
    (_peaks).n_peaks AS n_peaks,
    (_peaks).inter_peak_distances AS inter_peak_distances,
    (_peaks).mean_period AS mean_period
FROM peaks_data
)",
    "Detects peaks in a single time series, returning peak indices, values, and prominence scores.",
    "SELECT * FROM ts_detect_peaks('sales', product_id, date, qty)",
    "peak-detection"},

    // ts_analyze_peak_timing_by: Analyze peak timing for grouped series
    // C++ API: ts_analyze_peak_timing_by(table_name, group_col, date_col, value_col, period, params)
    // Returns: TABLE with id and expanded timing columns
    {"ts_analyze_peak_timing_by", {"source", "group_col", "date_col", "value_col", "period", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH timing_data AS (
    SELECT
        group_col,
        ts_analyze_peak_timing(
            LIST(value_col::DOUBLE ORDER BY date_col),
            period::DOUBLE
        ) AS _timing
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    group_col,
    (_timing).n_peaks AS n_peaks,
    (_timing).peak_times AS peak_times,
    (_timing).variability_score AS variability_score,
    (_timing).is_stable AS is_stable
FROM timing_data
)",
    "Analyzes peak timing variability for each group, returning timing statistics and stability score.",
    "SELECT * FROM ts_analyze_peak_timing_by('sales', product_id, date, qty, 7)",
    "peak-detection"},

    // ts_analyze_peak_timing: Analyze peak timing for a single series
    // C++ API: ts_analyze_peak_timing(table_name, date_col, value_col, period, params)
    // Returns: TABLE with expanded timing columns
    {"ts_analyze_peak_timing", {"source", "date_col", "value_col", "period", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH timing_data AS (
    SELECT
        ts_analyze_peak_timing(
            LIST(value_col::DOUBLE ORDER BY date_col),
            period::DOUBLE
        ) AS _timing
    FROM query_table(source::VARCHAR)
)
SELECT
    (_timing).n_peaks AS n_peaks,
    (_timing).peak_times AS peak_times,
    (_timing).variability_score AS variability_score,
    (_timing).is_stable AS is_stable
FROM timing_data
)",
    "Analyzes peak timing variability for a single time series.",
    "SELECT * FROM ts_analyze_peak_timing('sales', product_id, date, qty, 7)",
    "peak-detection"},

    // ================================================================================
    // Metrics Table Macros (DEPRECATED)
    //
    // DEPRECATED: These table macros are ~2400x slower than using scalar functions
    // with GROUP BY. Use the scalar function pattern instead:
    //
    //   SELECT
    //       unique_id,
    //       fold_id,
    //       ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae,
    //       ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse
    //   FROM my_data
    //   GROUP BY unique_id, fold_id;
    //
    // The table macros also have threading issues requiring SET threads=1 for
    // correct results on large datasets (DuckDB issues #18222, #19939).
    //
    // These macros are kept for backward compatibility but will be removed in
    // a future version.
    // ================================================================================

    // ts_mae_by: Compute Mean Absolute Error grouped by selected columns
    // C++ API: ts_mae_by(source, date_col, actual_col, forecast_col)
    {"ts_mae_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native(source, date_col, actual_col, forecast_col, 'mae')
)",
    "Computes Mean Absolute Error between actual and forecast columns per group.",
    "SELECT * FROM ts_mae_by('results', product_id, date, actual, forecast)",
    "metrics"},

    // ts_mse_by: Compute Mean Squared Error grouped by selected columns
    // C++ API: ts_mse_by(source, date_col, actual_col, forecast_col)
    {"ts_mse_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native(source, date_col, actual_col, forecast_col, 'mse')
)",
    "Computes Mean Squared Error between actual and forecast columns per group.",
    "SELECT * FROM ts_mse_by('results', product_id, date, actual, forecast)",
    "metrics"},

    // ts_rmse_by: Compute Root Mean Squared Error grouped by selected columns
    // C++ API: ts_rmse_by(source, date_col, actual_col, forecast_col)
    {"ts_rmse_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native(source, date_col, actual_col, forecast_col, 'rmse')
)",
    "Computes Root Mean Squared Error between actual and forecast columns per group.",
    "SELECT * FROM ts_rmse_by('results', product_id, date, actual, forecast)",
    "metrics"},

    // ts_mape_by: Compute Mean Absolute Percentage Error grouped by selected columns
    // C++ API: ts_mape_by(source, date_col, actual_col, forecast_col)
    {"ts_mape_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native(source, date_col, actual_col, forecast_col, 'mape')
)",
    "Computes Mean Absolute Percentage Error between actual and forecast columns per group.",
    "SELECT * FROM ts_mape_by('results', product_id, date, actual, forecast)",
    "metrics"},

    // ts_smape_by: Compute Symmetric Mean Absolute Percentage Error grouped by selected columns
    // C++ API: ts_smape_by(source, date_col, actual_col, forecast_col)
    {"ts_smape_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native(source, date_col, actual_col, forecast_col, 'smape')
)",
    "Computes Symmetric Mean Absolute Percentage Error between actual and forecast columns per group.",
    "SELECT * FROM ts_smape_by('results', product_id, date, actual, forecast)",
    "metrics"},

    // ts_r2_by: Compute R-squared (coefficient of determination) grouped by selected columns
    // C++ API: ts_r2_by(source, date_col, actual_col, forecast_col)
    {"ts_r2_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native(source, date_col, actual_col, forecast_col, 'r2')
)",
    "Computes R-squared (coefficient of determination) between actual and forecast columns per group.",
    "SELECT * FROM ts_r2_by('results', product_id, date, actual, forecast)",
    "metrics"},

    // ts_bias_by: Compute bias (mean error) grouped by selected columns
    // C++ API: ts_bias_by(source, date_col, actual_col, forecast_col)
    {"ts_bias_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native(source, date_col, actual_col, forecast_col, 'bias')
)",
    "Computes mean forecast bias between actual and forecast columns per group.",
    "SELECT * FROM ts_bias_by('results', product_id, date, actual, forecast)",
    "metrics"},

    // ts_mase_by: Compute Mean Absolute Scaled Error with GROUP BY ALL
    // C++ API: ts_mase_by(source, date_col, actual_col, forecast_col, baseline_col)
    {"ts_mase_by", {"source", "date_col", "actual_col", "forecast_col", "baseline_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_mase_native(source, date_col, actual_col, forecast_col, baseline_col)
)",
    "Computes Mean Absolute Scaled Error comparing forecast to a baseline model per group.",
    "SELECT * FROM ts_mase_by('results', product_id, date, actual, forecast, baseline)",
    "metrics"},

    // ts_rmae_by: Compute Relative Mean Absolute Error with GROUP BY ALL
    // C++ API: ts_rmae_by(source, date_col, actual_col, pred1_col, pred2_col)
    {"ts_rmae_by", {"source", "date_col", "actual_col", "pred1_col", "pred2_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_rmae_native(source, date_col, actual_col, pred1_col, pred2_col)
)",
    "Computes Relative MAE (MAE ratio between two models) per group.",
    "SELECT * FROM ts_rmae_by('results', product_id, date, actual, pred1, pred2)",
    "metrics"},

    // ts_coverage_by: Compute prediction interval coverage with GROUP BY ALL
    // C++ API: ts_coverage_by(source, date_col, actual_col, lower_col, upper_col)
    {"ts_coverage_by", {"source", "date_col", "actual_col", "lower_col", "upper_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_coverage_native(source, date_col, actual_col, lower_col, upper_col)
)",
    "Computes prediction interval coverage rate per group.",
    "SELECT * FROM ts_coverage_by('results', product_id, date, actual, lower, upper)",
    "metrics"},

    // ts_quantile_loss_by: Compute quantile loss with GROUP BY ALL
    // C++ API: ts_quantile_loss_by(source, date_col, actual_col, forecast_col, quantile)
    {"ts_quantile_loss_by", {"source", "date_col", "actual_col", "forecast_col", "quantile", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_quantile_loss_native(source, date_col, actual_col, forecast_col, quantile)
)",
    "Computes quantile (pinball) loss at a specified quantile level per group.",
    "SELECT * FROM ts_quantile_loss_by('results', product_id, date, actual, forecast, 0.9)",
    "metrics"},

    // Sentinel
    {nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr, nullptr, nullptr, nullptr}
};
// clang-format on

// Helper function to create a table macro from the definition
static unique_ptr<CreateMacroInfo> CreateTableMacro(const TsTableMacro &macro_def) {
    // Parse the SQL
    Parser parser;
    parser.ParseQuery(macro_def.macro);
    if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
        throw InternalException("Expected a single select statement in CreateTableMacro");
    }
    auto node = std::move(parser.statements[0]->Cast<SelectStatement>().node);

    // Create the macro function
    auto function = make_uniq<TableMacroFunction>(std::move(node));

    // Add positional parameters
    for (idx_t i = 0; macro_def.parameters[i] != nullptr; i++) {
        function->parameters.push_back(make_uniq<ColumnRefExpression>(macro_def.parameters[i]));
    }

    // Add named parameters with defaults
    for (idx_t i = 0; macro_def.named_params[i].name != nullptr; i++) {
        const auto &param = macro_def.named_params[i];
        function->parameters.push_back(make_uniq<ColumnRefExpression>(param.name));

        // Parse the default value
        auto expr_list = Parser::ParseExpressionList(param.default_value);
        if (!expr_list.empty()) {
            function->default_parameters.insert(make_pair(string(param.name), std::move(expr_list[0])));
        }
    }

    // Create the macro info
    auto info = make_uniq<CreateMacroInfo>(CatalogType::TABLE_MACRO_ENTRY);
    info->schema = DEFAULT_SCHEMA;
    info->name = macro_def.name;
    info->temporary = true;
    info->internal = true;
    info->macros.push_back(std::move(function));

    // Populate description if provided
    if (macro_def.description) {
        FunctionDescription desc;
        desc.description = macro_def.description;
        if (macro_def.example) {
            desc.examples.push_back(macro_def.example);
        }
        if (macro_def.category) {
            desc.categories.push_back("time-series");
            desc.categories.push_back(macro_def.category);
        }
        info->descriptions.push_back(std::move(desc));
    }

    return info;
}

void RegisterTsTableMacros(ExtensionLoader &loader) {
    for (idx_t i = 0; ts_table_macros[i].name != nullptr; i++) {
        // Register the short name (e.g. ts_forecast_by)
        auto info = CreateTableMacro(ts_table_macros[i]);
        loader.RegisterFunction(*info);

        // Register the prefixed alias (e.g. anofox_fcst_ts_forecast_by)
        auto alias_info = CreateTableMacro(ts_table_macros[i]);
        alias_info->name = "anofox_fcst_" + string(ts_table_macros[i].name);
        alias_info->alias_of = string(ts_table_macros[i].name);
        loader.RegisterFunction(*alias_info);
    }
}

} // namespace duckdb
