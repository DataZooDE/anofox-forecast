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
    const char *parameters[8];          // Positional parameters (nullptr terminated)
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
    ts_stats(LIST(value_col ORDER BY date_col)) AS stats
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
    ts_data_quality(LIST(value_col ORDER BY date_col)) AS quality
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
        ts_data_quality(LIST(value_col ORDER BY date_col)) AS quality
    FROM query_table(source::VARCHAR)
    GROUP BY unique_id_col
)
)"},

    // ts_drop_constant: Filter out constant series
    {"ts_drop_constant", {"source", "group_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH series_stats AS (
    SELECT
        group_col,
        ts_is_constant(LIST(value_col)) AS is_const
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT t.*
FROM query_table(source::VARCHAR) t
JOIN series_stats s ON t.group_col = s.group_col
WHERE NOT s.is_const
)"},

    // ts_drop_short: Filter out short series
    // C++ API: ts_drop_short(table_name, group_col, min_length)
    {"ts_drop_short", {"source", "group_col", "min_length", nullptr}, {{nullptr, nullptr}},
R"(
WITH series_counts AS (
    SELECT group_col, COUNT(*) AS n
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT t.*
FROM query_table(source::VARCHAR) t
JOIN series_counts c ON t.group_col = c.group_col
WHERE c.n >= min_length
)"},

    // ts_drop_leading_zeros: Remove leading zeros from series
    {"ts_drop_leading_zeros", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH first_nonzero AS (
    SELECT group_col, MIN(date_col) AS first_date
    FROM query_table(source::VARCHAR)
    WHERE value_col != 0 AND value_col IS NOT NULL
    GROUP BY group_col
)
SELECT t.*
FROM query_table(source::VARCHAR) t
JOIN first_nonzero f ON t.group_col = f.group_col
WHERE t.date_col >= f.first_date
)"},

    // ts_drop_trailing_zeros: Remove trailing zeros from series
    {"ts_drop_trailing_zeros", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH last_nonzero AS (
    SELECT group_col, MAX(date_col) AS last_date
    FROM query_table(source::VARCHAR)
    WHERE value_col != 0 AND value_col IS NOT NULL
    GROUP BY group_col
)
SELECT t.*
FROM query_table(source::VARCHAR) t
JOIN last_nonzero l ON t.group_col = l.group_col
WHERE t.date_col <= l.last_date
)"},

    // ts_drop_edge_zeros: Remove both leading and trailing zeros
    {"ts_drop_edge_zeros", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH nonzero_bounds AS (
    SELECT
        group_col,
        MIN(date_col) AS first_date,
        MAX(date_col) AS last_date
    FROM query_table(source::VARCHAR)
    WHERE value_col != 0 AND value_col IS NOT NULL
    GROUP BY group_col
)
SELECT t.*
FROM query_table(source::VARCHAR) t
JOIN nonzero_bounds b ON t.group_col = b.group_col
WHERE t.date_col >= b.first_date AND t.date_col <= b.last_date
)"},

    // ts_fill_nulls_const: Fill NULL values with constant
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

    // ts_fill_nulls_forward: Forward fill (LOCF)
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

    // ts_fill_nulls_backward: Backward fill (NOCB)
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

    // ts_fill_nulls_mean: Fill with series mean
    {"ts_fill_nulls_mean", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH series_means AS (
    SELECT group_col, AVG(value_col) AS mean_val
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    t.group_col,
    t.date_col,
    COALESCE(t.value_col, m.mean_val) AS value_col
FROM query_table(source::VARCHAR) t
JOIN series_means m ON t.group_col = m.group_col
ORDER BY t.group_col, t.date_col
)"},

    // ts_diff: Compute differences
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
    {"ts_fill_gaps", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
date_range AS (
    SELECT
        _grp,
        MIN(_dt)::TIMESTAMP AS _min_dt,
        MAX(_dt)::TIMESTAMP AS _max_dt
    FROM src
    GROUP BY _grp
),
all_dates AS (
    SELECT
        dr._grp,
        UNNEST(generate_series(dr._min_dt, dr._max_dt, frequency::INTERVAL)) AS _dt
    FROM date_range dr
)
SELECT
    ad._grp AS group_col,
    ad._dt AS date_col,
    s._val AS value_col
FROM all_dates ad
LEFT JOIN src s ON ad._grp = s._grp AND ad._dt = s._dt
ORDER BY ad._grp, ad._dt
)"},

    // ts_fill_forward: Fill forward to a target date with NULL values
    // C++ API: ts_fill_forward(table_name, group_col, date_col, value_col, target_date, frequency)
    {"ts_fill_forward", {"source", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
last_dates AS (
    SELECT
        _grp,
        MAX(_dt)::TIMESTAMP AS _max_dt
    FROM src
    GROUP BY _grp
),
forward_dates AS (
    SELECT
        ld._grp,
        UNNEST(generate_series(ld._max_dt + frequency::INTERVAL, target_date::TIMESTAMP, frequency::INTERVAL)) AS _dt
    FROM last_dates ld
    WHERE ld._max_dt < target_date::TIMESTAMP
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
    {"ts_fill_gaps_operator", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
date_range AS (
    SELECT
        _grp,
        MIN(_dt)::TIMESTAMP AS _min_dt,
        MAX(_dt)::TIMESTAMP AS _max_dt
    FROM src
    GROUP BY _grp
),
all_dates AS (
    SELECT
        dr._grp,
        UNNEST(generate_series(dr._min_dt, dr._max_dt, frequency::INTERVAL)) AS _dt
    FROM date_range dr
)
SELECT
    ad._grp AS group_col,
    ad._dt AS date_col,
    s._val AS value_col
FROM all_dates ad
LEFT JOIN src s ON ad._grp = s._grp AND ad._dt = s._dt
ORDER BY ad._grp, ad._dt
)"},

    // ts_fill_forward_operator: Fill forward to a target date with NULL values (operator version)
    // C++ API: ts_fill_forward_operator(table_name, group_col, date_col, value_col, target_date, frequency)
    // Note: This is identical to ts_fill_forward but named "operator" for C++ API compatibility
    {"ts_fill_forward_operator", {"source", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
last_dates AS (
    SELECT
        _grp,
        MAX(_dt)::TIMESTAMP AS _max_dt
    FROM src
    GROUP BY _grp
),
forward_dates AS (
    SELECT
        ld._grp,
        UNNEST(generate_series(ld._max_dt + frequency::INTERVAL, target_date::TIMESTAMP, frequency::INTERVAL)) AS _dt
    FROM last_dates ld
    WHERE ld._max_dt < target_date::TIMESTAMP
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

    // ts_mstl_decomposition: MSTL decomposition for grouped series
    // C++ API: ts_mstl_decomposition(table_name, group_col, date_col, value_col, params MAP)
    {"ts_mstl_decomposition", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS id,
    ts_mstl_decomposition(LIST(value_col ORDER BY date_col), params['seasonal_periods']) AS decomposition
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
        date_col,
        value_col,
        ROW_NUMBER() OVER (ORDER BY date_col) AS idx
    FROM query_table(source::VARCHAR)
),
cp_result AS (
    SELECT ts_detect_changepoints_bocpd(
        LIST(value_col ORDER BY date_col),
        COALESCE(params['hazard_lambda']::DOUBLE, 250.0),
        COALESCE(params['include_probabilities']::BOOLEAN, false)
    ) AS cp
    FROM query_table(source::VARCHAR)
)
SELECT
    od.date_col,
    od.value_col,
    (cp.cp).is_changepoint[od.idx] AS is_changepoint,
    (cp.cp).changepoint_probability[od.idx] AS changepoint_probability
FROM ordered_data od, cp_result cp
ORDER BY od.date_col
)"},

    // ts_detect_changepoints_by: Detect changepoints per group
    // C++ API: ts_detect_changepoints_by(table_name, group_col, date_col, value_col, params MAP)
    {"ts_detect_changepoints_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS id,
    ts_detect_changepoints_bocpd(
        LIST(value_col ORDER BY date_col),
        COALESCE(params['hazard_lambda']::DOUBLE, 250.0),
        COALESCE(params['include_probabilities']::BOOLEAN, false)
    ) AS changepoints
FROM query_table(source::VARCHAR)
GROUP BY group_col
)"},

    // ts_forecast: Generate forecasts for a single series
    // Signature: ts_forecast(table_name, date_col, target_col, method, horizon, params)
    {"ts_forecast", {"source", "date_col", "target_col", "method", "horizon", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH forecast_result AS (
    SELECT ts_forecast(LIST(target_col ORDER BY date_col), horizon, method) AS fcst
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

    // ts_forecast_by: Generate forecasts per group
    // Signature: ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, params)
    {"ts_forecast_by", {"source", "group_col", "date_col", "target_col", "method", "horizon", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS id,
    ts_forecast(LIST(target_col ORDER BY date_col), horizon, method) AS forecast
FROM query_table(source::VARCHAR)
GROUP BY group_col
)"},

    // anofox_fcst_ts_forecast: Alias for ts_forecast
    // Signature: anofox_fcst_ts_forecast(table_name, date_col, target_col, method, horizon, params)
    {"anofox_fcst_ts_forecast", {"source", "date_col", "target_col", "method", "horizon", "params", nullptr}, {{nullptr, nullptr}},
R"(
WITH forecast_result AS (
    SELECT ts_forecast(LIST(target_col ORDER BY date_col), horizon, method) AS fcst
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

    // anofox_fcst_ts_forecast_by: Alias for ts_forecast_by
    // Signature: anofox_fcst_ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, params)
    {"anofox_fcst_ts_forecast_by", {"source", "group_col", "date_col", "target_col", "method", "horizon", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT
    group_col AS id,
    ts_forecast(LIST(target_col ORDER BY date_col), horizon, method) AS forecast
FROM query_table(source::VARCHAR)
GROUP BY group_col
)"},

    // anofox_fcst_ts_fill_forward_operator: Alias for ts_fill_forward_operator
    // Signature: anofox_fcst_ts_fill_forward_operator(table_name, group_col, date_col, value_col, target_date, frequency)
    {"anofox_fcst_ts_fill_forward_operator", {"source", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH src AS (
    SELECT group_col AS _grp, date_col AS _dt, value_col AS _val
    FROM query_table(source::VARCHAR)
),
last_dates AS (
    SELECT
        _grp,
        MAX(_dt)::TIMESTAMP AS _max_dt
    FROM src
    GROUP BY _grp
),
forward_dates AS (
    SELECT
        ld._grp,
        UNNEST(generate_series(ld._max_dt + frequency::INTERVAL, target_date::TIMESTAMP, frequency::INTERVAL)) AS _dt
    FROM last_dates ld
    WHERE ld._max_dt < target_date::TIMESTAMP
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
