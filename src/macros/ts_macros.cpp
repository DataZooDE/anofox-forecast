#include "anofox_forecast_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/parsed_data/create_macro_info.hpp"
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
};

// clang-format off
static const TsTableMacro ts_table_macros[] = {
    // ts_stats: Compute statistics for grouped time series
    // C++ API: ts_stats(table_name, group_col, date_col, value_col, frequency)
    {"ts_stats", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS id,
    _ts_stats(LIST(value_col ORDER BY date_col)) AS stats
FROM query_table(source::VARCHAR)
GROUP BY group_col
)"},

    // ts_quality_report: Generate quality report from stats table
    // C++ API: ts_quality_report(stats_table, min_length)
    {"ts_quality_report", {"stats_table", "min_length", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    SUM(CASE WHEN (stats).length >= min_length AND NOT (stats).is_constant THEN 1 ELSE 0 END) AS n_passed,
    SUM(CASE WHEN (stats).n_gaps > 0 THEN 1 ELSE 0 END) AS n_gap_issues,
    SUM(CASE WHEN (stats).n_nulls > 0 THEN 1 ELSE 0 END) AS n_missing_issues,
    SUM(CASE WHEN (stats).is_constant THEN 1 ELSE 0 END) AS n_constant,
    COUNT(*) AS n_total
FROM query_table(stats_table::VARCHAR)
)"},

    // ts_stats_summary: Summary statistics from stats table
    // C++ API: ts_stats_summary(stats_table)
    {"ts_stats_summary", {"stats_table", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    COUNT(*) AS n_series,
    AVG((stats).length) AS avg_length,
    MIN((stats).length) AS min_length,
    MAX((stats).length) AS max_length,
    SUM((stats).n_nulls) AS total_nulls,
    SUM((stats).n_gaps) AS total_gaps
FROM query_table(stats_table::VARCHAR)
)"},

    // ts_data_quality: Assess data quality per series
    // C++ API: ts_data_quality(table_name, unique_id_col, date_col, value_col, n_short, frequency)
    {"ts_data_quality", {"source", "unique_id_col", "date_col", "value_col", "n_short", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    unique_id_col AS unique_id,
    _ts_data_quality(LIST(value_col ORDER BY date_col)) AS quality
FROM query_table(source::VARCHAR)
GROUP BY unique_id_col
)"},

    // ts_data_quality_summary: Summarize data quality across series
    // C++ API: ts_data_quality_summary(table_name, unique_id_col, date_col, value_col, n_short)
    {"ts_data_quality_summary", {"source", "unique_id_col", "date_col", "value_col", "n_short", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    COUNT(*) AS n_total,
    SUM(CASE WHEN (quality).overall_score >= 0.8 THEN 1 ELSE 0 END) AS n_good,
    SUM(CASE WHEN (quality).overall_score >= 0.5 AND (quality).overall_score < 0.8 THEN 1 ELSE 0 END) AS n_fair,
    SUM(CASE WHEN (quality).overall_score < 0.5 THEN 1 ELSE 0 END) AS n_poor,
    AVG((quality).overall_score) AS avg_score
FROM (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(LIST(value_col ORDER BY date_col)) AS quality
    FROM query_table(source::VARCHAR)
    GROUP BY unique_id_col
)
)"},

    // ts_drop_constant: Filter out constant series (table-based)
    // C++ API: ts_drop_constant(table_name, group_col, value_col)
    {"ts_drop_constant", {"source", "group_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING MIN(value_col) != MAX(value_col) OR MIN(value_col) IS NULL OR MAX(value_col) IS NULL
)
)"},

    // ts_drop_short: Filter out short series (table-based)
    // C++ API: ts_drop_short(table_name, group_col, min_length)
    {"ts_drop_short", {"source", "group_col", "min_length", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING COUNT(*) >= min_length
)
)"},

    // ts_drop_leading_zeros: Remove leading zeros from series (table-based)
    // C++ API: ts_drop_leading_zeros(table_name, group_col, date_col, value_col)
    {"ts_drop_leading_zeros", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH first_nonzero AS (
    SELECT *,
           MIN(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN date_col END) OVER (PARTITION BY group_col) AS _first_nz
    FROM query_table(source::VARCHAR)
)
SELECT * EXCLUDE (_first_nz)
FROM first_nonzero
WHERE date_col >= _first_nz
)"},

    // ts_drop_trailing_zeros: Remove trailing zeros from series (table-based)
    // C++ API: ts_drop_trailing_zeros(table_name, group_col, date_col, value_col)
    {"ts_drop_trailing_zeros", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH last_nonzero AS (
    SELECT *,
           MAX(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN date_col END) OVER (PARTITION BY group_col) AS _last_nz
    FROM query_table(source::VARCHAR)
)
SELECT * EXCLUDE (_last_nz)
FROM last_nonzero
WHERE date_col <= _last_nz
)"},

    // ts_drop_edge_zeros: Remove both leading and trailing zeros (table-based)
    // C++ API: ts_drop_edge_zeros(table_name, group_col, date_col, value_col)
    {"ts_drop_edge_zeros", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
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
)"},

    // ts_fill_nulls_const: Fill NULL values with constant (table-based)
    // C++ API: ts_fill_nulls_const(table_name, group_col, date_col, value_col, fill_value)
    {"ts_fill_nulls_const", {"source", "group_col", "date_col", "value_col", "fill_value", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col,
    date_col,
    COALESCE(value_col, fill_value) AS value_col
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)"},

    // ts_fill_nulls_forward: Forward fill (LOCF) (table-based)
    // C++ API: ts_fill_nulls_forward(table_name, group_col, date_col, value_col)
    {"ts_fill_nulls_forward", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col,
    date_col,
    COALESCE(value_col, LAG(value_col IGNORE NULLS) OVER (
        PARTITION BY group_col ORDER BY date_col
    )) AS value_col
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)"},

    // ts_fill_nulls_backward: Backward fill (NOCB) (table-based)
    // C++ API: ts_fill_nulls_backward(table_name, group_col, date_col, value_col)
    {"ts_fill_nulls_backward", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col,
    date_col,
    COALESCE(value_col, LEAD(value_col IGNORE NULLS) OVER (
        PARTITION BY group_col ORDER BY date_col
    )) AS value_col
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)"},

    // ts_fill_nulls_mean: Fill with series mean (table-based)
    // C++ API: ts_fill_nulls_mean(table_name, group_col, date_col, value_col)
    {"ts_fill_nulls_mean", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH with_mean AS (
    SELECT *,
           AVG(value_col) OVER (PARTITION BY group_col) AS _mean_val
    FROM query_table(source::VARCHAR)
)
SELECT group_col, date_col, COALESCE(value_col, _mean_val) AS value_col
FROM with_mean
ORDER BY group_col, date_col
)"},

    // ts_diff: Compute differences (table-based)
    // C++ API: ts_diff(table_name, group_col, date_col, value_col, diff_order)
    {"ts_diff", {"source", "group_col", "date_col", "value_col", "diff_order", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col,
    date_col,
    value_col - LAG(value_col, diff_order) OVER (
        PARTITION BY group_col ORDER BY date_col
    ) AS diff_value
FROM query_table(source::VARCHAR)
ORDER BY group_col, date_col
)"},

    // ts_fill_gaps: Fill date gaps with NULL values at specified frequency
    // C++ API: ts_fill_gaps(table_name, group_col, date_col, value_col, frequency)
    // Supports both Polars-style ('1d', '1h', '30m', '1w', '1mo', '1q', '1y') and DuckDB INTERVAL ('1 day', '1 hour')
    {"ts_fill_gaps", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH _freq AS (
    SELECT CASE
        WHEN frequency ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency, 'd$', ' day'))::INTERVAL
        WHEN frequency ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency, 'h$', ' hour'))::INTERVAL
        WHEN frequency ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency, 'w$', ' week'))::INTERVAL
        WHEN frequency ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency, 'mo$', ' month'))::INTERVAL
        WHEN frequency ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
date_range AS (
    SELECT
        _grp,
        date_trunc('second', MIN(_dt)::TIMESTAMP) AS _min_dt,
        date_trunc('second', MAX(_dt)::TIMESTAMP) AS _max_dt
    FROM src
    GROUP BY _grp
),
all_dates AS (
    SELECT
        dr._grp,
        UNNEST(generate_series(dr._min_dt, dr._max_dt, (SELECT _interval FROM _freq))) AS _dt
    FROM date_range dr
)
SELECT
    ad._grp AS group_col,
    ad._dt AS date_col,
    s._val AS value_col
FROM all_dates ad
LEFT JOIN src s ON ad._grp = s._grp AND date_trunc('second', ad._dt) = date_trunc('second', s._dt::TIMESTAMP)
ORDER BY ad._grp, ad._dt
)"},

    // ts_fill_forward: Fill forward to a target date with NULL values
    // C++ API: ts_fill_forward(table_name, group_col, date_col, value_col, target_date, frequency)
    // Supports both Polars-style ('1d', '1h', '30m', '1w', '1mo', '1q', '1y') and DuckDB INTERVAL ('1 day', '1 hour')
    {"ts_fill_forward", {"source", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH _freq AS (
    SELECT CASE
        WHEN frequency ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency, 'd$', ' day'))::INTERVAL
        WHEN frequency ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency, 'h$', ' hour'))::INTERVAL
        WHEN frequency ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency, 'w$', ' week'))::INTERVAL
        WHEN frequency ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency, 'mo$', ' month'))::INTERVAL
        WHEN frequency ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
last_dates AS (
    SELECT
        _grp,
        date_trunc('second', MAX(_dt)::TIMESTAMP) AS _max_dt
    FROM src
    GROUP BY _grp
),
forward_dates AS (
    SELECT
        ld._grp,
        UNNEST(generate_series(ld._max_dt + (SELECT _interval FROM _freq), date_trunc('second', target_date::TIMESTAMP), (SELECT _interval FROM _freq))) AS _dt
    FROM last_dates ld
    WHERE ld._max_dt < date_trunc('second', target_date::TIMESTAMP)
)
SELECT _grp AS group_col, _dt AS date_col, _val AS value_col FROM src
UNION ALL
SELECT
    fd._grp AS group_col,
    fd._dt AS date_col,
    NULL::DOUBLE AS value_col
FROM forward_dates fd
ORDER BY 1, 2
)"},

    // ts_fill_gaps_operator: High-performance gap filling (API compatible with C++ version)
    // C++ API: ts_fill_gaps_operator(table_name, group_col, date_col, value_col, frequency)
    // Note: This is identical to ts_fill_gaps but named "operator" for C++ API compatibility
    // Supports both Polars-style ('1d', '1h', '30m', '1w', '1mo', '1q', '1y') and DuckDB INTERVAL ('1 day', '1 hour')
    {"ts_fill_gaps_operator", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH _freq AS (
    SELECT CASE
        WHEN frequency ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency, 'd$', ' day'))::INTERVAL
        WHEN frequency ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency, 'h$', ' hour'))::INTERVAL
        WHEN frequency ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency, 'w$', ' week'))::INTERVAL
        WHEN frequency ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency, 'mo$', ' month'))::INTERVAL
        WHEN frequency ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
date_range AS (
    SELECT
        _grp,
        date_trunc('second', MIN(_dt)::TIMESTAMP) AS _min_dt,
        date_trunc('second', MAX(_dt)::TIMESTAMP) AS _max_dt
    FROM src
    GROUP BY _grp
),
all_dates AS (
    SELECT
        dr._grp,
        UNNEST(generate_series(dr._min_dt, dr._max_dt, (SELECT _interval FROM _freq))) AS _dt
    FROM date_range dr
)
SELECT
    ad._grp AS group_col,
    ad._dt AS date_col,
    s._val AS value_col
FROM all_dates ad
LEFT JOIN src s ON ad._grp = s._grp AND date_trunc('second', ad._dt) = date_trunc('second', s._dt::TIMESTAMP)
ORDER BY ad._grp, ad._dt
)"},

    // ts_drop_gappy: Filter out series with too many gaps
    // C++ API: ts_drop_gappy(table_name, group_col, value_col, max_gap_ratio)
    {"ts_drop_gappy", {"source", "group_col", "value_col", "max_gap_ratio", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING (SUM(CASE WHEN value_col IS NULL THEN 1 ELSE 0 END)::DOUBLE / NULLIF(COUNT(*), 0)) <= max_gap_ratio
)
)"},

    // ts_drop_zeros: Filter out series with all zeros
    // C++ API: ts_drop_zeros(table_name, group_col, value_col)
    {"ts_drop_zeros", {"source", "group_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING SUM(CASE WHEN value_col != 0 AND value_col IS NOT NULL THEN 1 ELSE 0 END) > 0
)
)"},

    // ts_mstl_decomposition: MSTL decomposition for grouped series
    // C++ API: ts_mstl_decomposition(table_name, group_col, date_col, value_col, params MAP)
    {"ts_mstl_decomposition", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS id,
    _ts_mstl_decomposition(LIST(value_col ORDER BY date_col)) AS decomposition
FROM query_table(source::VARCHAR)
GROUP BY group_col
)"},

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
        LIST(value_col ORDER BY date_col),
        COALESCE(params['hazard_lambda']::DOUBLE, 250.0),
        COALESCE(params['include_probabilities']::BOOLEAN, false)
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
)"},

    // ts_detect_changepoints_by: Detect changepoints per group
    // C++ API: ts_detect_changepoints_by(table_name, group_col, date_col, value_col, params MAP)
    {"ts_detect_changepoints_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS id,
    _ts_detect_changepoints_bocpd(
        LIST(value_col ORDER BY date_col),
        COALESCE(params['hazard_lambda']::DOUBLE, 250.0),
        COALESCE(params['include_probabilities']::BOOLEAN, false)
    ) AS changepoints
FROM query_table(source::VARCHAR)
GROUP BY group_col
)"},

    // ts_forecast: Generate forecasts for a single series (table-based)
    // C++ API: ts_forecast(table_name, date_col, target_col, method, horizon, params)
    {"ts_forecast", {"source", "date_col", "target_col", "method", "horizon", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH forecast_result AS (
    SELECT _ts_forecast(LIST(target_col ORDER BY date_col), horizon, method) AS fcst
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
)"},

    // ts_forecast_by: Generate forecasts per group (long format - one row per forecast step)
    // C++ API: ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, params, frequency)
    // Supports both Polars-style ('1d', '1h', '30m', '1w', '1mo', '1q', '1y') and DuckDB INTERVAL ('1 day', '1 hour')
    {"ts_forecast_by", {"source", "group_col", "date_col", "target_col", "method", "horizon", "params", nullptr}, {{"frequency", "'1d'"}, {nullptr, nullptr}},
R"(
WITH _freq AS (
    SELECT CASE
        WHEN frequency ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency, 'd$', ' day'))::INTERVAL
        WHEN frequency ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency, 'h$', ' hour'))::INTERVAL
        WHEN frequency ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency, 'w$', ' week'))::INTERVAL
        WHEN frequency ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency, 'mo$', ' month'))::INTERVAL
        WHEN frequency ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
forecast_data AS (
    SELECT
        group_col AS id,
        date_trunc('second', MAX(date_col)::TIMESTAMP) AS last_date,
        _ts_forecast(LIST(target_col ORDER BY date_col), horizon, method) AS fcst
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    id,
    UNNEST(generate_series(1, len((fcst).point)))::INTEGER AS forecast_step,
    UNNEST(generate_series(last_date + (SELECT _interval FROM _freq), last_date + (len((fcst).point)::INTEGER * EXTRACT(EPOCH FROM (SELECT _interval FROM _freq)) || ' seconds')::INTERVAL, (SELECT _interval FROM _freq)))::TIMESTAMP AS date,
    UNNEST((fcst).point) AS point_forecast,
    UNNEST((fcst).lower) AS lower_90,
    UNNEST((fcst).upper) AS upper_90,
    (fcst).model AS model_name,
    (fcst).fitted AS insample_fitted
FROM forecast_data
ORDER BY id, forecast_step
)"},

    // ts_forecast_exog: Generate forecasts with exogenous variables (single series)
    // C++ API: ts_forecast_exog(source, date_col, target_col, xreg_cols, future_source, future_date_col, future_xreg_cols, method, horizon, params)
    // - source: historical data table with y and X columns
    // - xreg_cols: list of X column names in source table
    // - future_source: table containing future X values
    // - future_xreg_cols: list of X column names in future_source table
    {"ts_forecast_exog", {"source", "date_col", "target_col", "xreg_cols", "future_source", "future_date_col", "future_xreg_cols", nullptr}, {{"method", "'AutoARIMA'"}, {"horizon", "12"}, {"params", "MAP{}"}, {nullptr, nullptr}},
R"(
WITH historical_data AS (
    SELECT
        target_col AS _y,
        LIST_TRANSFORM(xreg_cols, col -> query_table(source::VARCHAR)[col]) AS _x_cols
    FROM query_table(source::VARCHAR)
    ORDER BY date_col
),
agg_historical AS (
    SELECT
        LIST(_y ORDER BY ROWID) AS _y_list,
        LIST_TRANSFORM(RANGE(LEN(xreg_cols)), i ->
            LIST(_x_cols[i + 1] ORDER BY ROWID)
        ) AS _xreg_list
    FROM historical_data
),
future_data AS (
    SELECT
        LIST_TRANSFORM(future_xreg_cols, col -> query_table(future_source::VARCHAR)[col]) AS _fx_cols
    FROM query_table(future_source::VARCHAR)
    ORDER BY future_date_col
    LIMIT horizon
),
agg_future AS (
    SELECT
        LIST_TRANSFORM(RANGE(LEN(future_xreg_cols)), i ->
            LIST(_fx_cols[i + 1] ORDER BY ROWID)
        ) AS _future_xreg_list
    FROM future_data
),
forecast_result AS (
    SELECT _ts_forecast_exog(
        (SELECT _y_list FROM agg_historical),
        (SELECT _xreg_list FROM agg_historical),
        (SELECT _future_xreg_list FROM agg_future),
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
)"},

    // ts_forecast_exog_by: Generate forecasts with exogenous variables per group
    // C++ API: ts_forecast_exog_by(source, group_col, date_col, target_col, xreg_cols, future_source, future_date_col, future_xreg_cols, method, horizon, params, frequency)
    {"ts_forecast_exog_by", {"source", "group_col", "date_col", "target_col", "xreg_cols", "future_source", "future_date_col", "future_xreg_cols", nullptr}, {{"method", "'AutoARIMA'"}, {"horizon", "12"}, {"params", "MAP{}"}, {"frequency", "'1d'"}, {nullptr, nullptr}},
R"(
WITH _freq AS (
    SELECT CASE
        WHEN frequency ~ '^[0-9]+d$' THEN (REGEXP_REPLACE(frequency, 'd$', ' day'))::INTERVAL
        WHEN frequency ~ '^[0-9]+h$' THEN (REGEXP_REPLACE(frequency, 'h$', ' hour'))::INTERVAL
        WHEN frequency ~ '^[0-9]+(m|min)$' THEN (REGEXP_REPLACE(frequency, '(m|min)$', ' minute'))::INTERVAL
        WHEN frequency ~ '^[0-9]+w$' THEN (REGEXP_REPLACE(frequency, 'w$', ' week'))::INTERVAL
        WHEN frequency ~ '^[0-9]+mo$' THEN (REGEXP_REPLACE(frequency, 'mo$', ' month'))::INTERVAL
        WHEN frequency ~ '^[0-9]+q$' THEN ((CAST(REGEXP_EXTRACT(frequency, '^([0-9]+)', 1) AS INTEGER) * 3)::VARCHAR || ' month')::INTERVAL
        WHEN frequency ~ '^[0-9]+y$' THEN (REGEXP_REPLACE(frequency, 'y$', ' year'))::INTERVAL
        ELSE frequency::INTERVAL
    END AS _interval
),
src AS (
    SELECT * FROM query_table(source::VARCHAR)
),
future_src AS (
    SELECT * FROM query_table(future_source::VARCHAR)
),
grouped_historical AS (
    SELECT
        group_col AS id,
        date_trunc('second', MAX(date_col)::TIMESTAMP) AS last_date,
        LIST(target_col ORDER BY date_col) AS _y_list,
        LIST_TRANSFORM(xreg_cols, col ->
            (SELECT LIST(src2[col] ORDER BY src2.date_col) FROM src src2 WHERE src2.group_col = src.group_col)
        ) AS _xreg_list
    FROM src
    GROUP BY group_col
),
grouped_future AS (
    SELECT
        group_col AS id,
        LIST_TRANSFORM(future_xreg_cols, col ->
            (SELECT LIST(f2[col] ORDER BY f2.future_date_col) FROM future_src f2 WHERE f2.group_col = future_src.group_col LIMIT horizon)
        ) AS _future_xreg_list
    FROM future_src
    GROUP BY group_col
),
forecast_data AS (
    SELECT
        h.id,
        h.last_date,
        _ts_forecast_exog(
            h._y_list,
            h._xreg_list,
            COALESCE(f._future_xreg_list, LIST_TRANSFORM(xreg_cols, col -> []::DOUBLE[])),
            horizon,
            method
        ) AS fcst
    FROM grouped_historical h
    LEFT JOIN grouped_future f ON h.id = f.id
)
SELECT
    id,
    UNNEST(generate_series(1, len((fcst).point)))::INTEGER AS forecast_step,
    UNNEST(generate_series(last_date + (SELECT _interval FROM _freq), last_date + (len((fcst).point)::INTEGER * EXTRACT(EPOCH FROM (SELECT _interval FROM _freq)) || ' seconds')::INTERVAL, (SELECT _interval FROM _freq)))::TIMESTAMP AS date,
    UNNEST((fcst).point) AS point_forecast,
    UNNEST((fcst).lower) AS lower_90,
    UNNEST((fcst).upper) AS upper_90,
    (fcst).model AS model_name
FROM forecast_data
ORDER BY id, forecast_step
)"},

    // Sentinel
    {nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}
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

    return info;
}

void RegisterTsTableMacros(ExtensionLoader &loader) {
    for (idx_t i = 0; ts_table_macros[i].name != nullptr; i++) {
        auto info = CreateTableMacro(ts_table_macros[i]);
        loader.RegisterFunction(*info);
    }
}

} // namespace duckdb
