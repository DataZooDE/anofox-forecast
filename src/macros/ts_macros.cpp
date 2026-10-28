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
    // C++ API: ts_stats(table_name, group_col, date_col, value_col, frequency, seasonal_period := 0)
    // Returns: TABLE with expanded statistics columns
    {"ts_stats", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{"seasonal_period", "0"}, {nullptr, nullptr}},
R"(
WITH stats_data AS (
    SELECT
        group_col,
        _ts_stats_with_dates(
            -- CR-02 fix: drop NULL-dated rows (value+date together) before they reach
            -- the scalar function, matching ts_stats_by's/the aggregates' "drop this
            -- row" semantics for a NULL date_col, instead of aborting the whole query.
            LIST(value_col::DOUBLE ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            LIST(date_col::TIMESTAMP ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            frequency::VARCHAR,
            seasonal_period::INTEGER
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
    (_stats).n_gaps AS n_gaps,
    (_stats).n_gap_runs AS n_gap_runs
FROM stats_data
)",
    "Computes 36 time series statistics per group (including n_gap_runs). trend_strength/seasonality_strength are STL-based (D-02); seasonal_period (default 0) selects 0=auto-detect, 1=non-seasonal, >=2=explicit period. Returns a wide table with one column per statistic.",
    "SELECT * FROM ts_stats('sales', product_id, date, qty, '1d', seasonal_period := 12)",
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
    // D-05/WRAP-02: passes dates and frequency through to _ts_data_quality so
    // gap/temporal scoring reflects real gaps (plan 13-01's shared gap counter);
    // n_short drives the structural length factor and the new is_short flag.
    {"ts_data_quality", {"source", "unique_id_col", "date_col", "value_col", "n_short", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH quality_data AS (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(
            -- CR-02 fix: drop NULL-dated rows (value+date together) before they reach
            -- the scalar function, matching ts_stats_by's/the aggregates' "drop this
            -- row" semantics for a NULL date_col, instead of aborting the whole query.
            LIST(value_col::DOUBLE ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            LIST(date_col::TIMESTAMP ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            frequency::VARCHAR,
            n_short::INTEGER
        ) AS _quality
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
    (_quality).n_gap_runs AS n_gap_runs,
    (_quality).n_missing AS n_missing,
    (_quality).is_constant AS is_constant,
    (_quality).is_short AS is_short
FROM quality_data
)",
    "Assesses time series data quality per group (dates/frequency-aware gap and temporal scoring; n_short drives is_short), returning null rate, gap rate (n_gaps = missing periods, n_gap_runs = gap occurrences), duplicate count, and quality score.",
    "SELECT * FROM ts_data_quality('sales', product_id, date, qty, 10, '1d')",
    "data-quality"},

    // ts_data_quality_summary: Summarize data quality across series
    // C++ API: ts_data_quality_summary(table_name, unique_id_col, date_col, value_col, n_short)
    // No frequency parameter: auto-detects frequency per series (NULL passed to _ts_data_quality).
    {"ts_data_quality_summary", {"source", "unique_id_col", "date_col", "value_col", "n_short", nullptr}, {{nullptr, nullptr}},
R"(
WITH quality_data AS (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(
            -- CR-02 fix: drop NULL-dated rows (value+date together) before they reach
            -- the scalar function, matching ts_stats_by's/the aggregates' "drop this
            -- row" semantics for a NULL date_col, instead of aborting the whole query.
            LIST(value_col::DOUBLE ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            LIST(date_col::TIMESTAMP ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            NULL::VARCHAR,
            n_short::INTEGER
        ) AS _quality
    FROM query_table(source::VARCHAR)
    GROUP BY unique_id_col
)
SELECT
    COUNT(*) AS n_total,
    SUM(CASE WHEN (_quality).overall_score >= 0.8 THEN 1 ELSE 0 END) AS n_good,
    SUM(CASE WHEN (_quality).overall_score >= 0.5 AND (_quality).overall_score < 0.8 THEN 1 ELSE 0 END) AS n_fair,
    SUM(CASE WHEN (_quality).overall_score < 0.5 THEN 1 ELSE 0 END) AS n_poor,
    AVG((_quality).overall_score) AS avg_score,
    -- WR-05 (Phase 18, D-10): a group whose dates are all NULL has an empty
    -- FILTERed LIST, so _ts_data_quality returns a NULL struct for it; every
    -- CASE WHEN above takes its ELSE 0 branch (NULL >= x is NULL, not TRUE) and
    -- AVG() skips it, silently excluding the group from every bucket while
    -- COUNT(*) still counts it toward n_total. n_unassessed makes that group
    -- visible instead of silently dropped, restoring the invariant
    -- n_good + n_fair + n_poor + n_unassessed = n_total. avg_score remains the
    -- mean over assessed (non-NULL-struct) groups only.
    SUM(CASE WHEN (_quality).overall_score IS NULL THEN 1 ELSE 0 END) AS n_unassessed
FROM quality_data
)",
    "Summarizes ts_data_quality output across all groups into aggregate quality metrics (auto-detected frequency per series). "
    "n_unassessed counts groups whose quality struct is NULL (e.g. all dates NULL after filtering), so that "
    "n_good + n_fair + n_poor + n_unassessed = n_total; avg_score is the mean overall_score over assessed groups only.",
    "SELECT * FROM ts_data_quality_summary('sales', product_id, date, qty, 10)",
    "data-quality"},

    // ts_drop_constant_by: Filter out constant series (table-based), Rust is_constant (D-09)
    // C++ API: ts_drop_constant_by(table_name, group_col, value_col)
    {"ts_drop_constant_by", {"source", "group_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT *
FROM query_table(source::VARCHAR)
WHERE group_col IN (
    SELECT group_col
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
    HAVING NOT _ts_is_constant(LIST(value_col::DOUBLE))
)
)",
    "Removes groups whose time series classify as constant under the Rust is_constant tolerance "
    "(scale-relative: max - min <= 1e-10 * max(1, max|x|) over non-NULL/non-NaN values, D-13): "
    "fewer than two present values (including an all-NULL group), or all present values within "
    "that scale-relative tolerance of each other.",
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

    // ts_fill_nulls_mean_by: Fill with series mean via the Rust fill_nulls_mean (table-based)
    // C++ API: ts_fill_nulls_mean_by(table_name, group_col, date_col, value_col)
    // Note: All input columns are preserved; filled value is added as 'filled_value' column
    {"ts_fill_nulls_mean_by", {"source", "group_col", "date_col", "value_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH _src AS (
    SELECT *, ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY date_col) AS __ts_rn
    FROM query_table(source::VARCHAR)
),
_filled AS (
    SELECT
        group_col AS __ts_g,
        _ts_fill_nulls_mean(LIST(value_col::DOUBLE ORDER BY __ts_rn)) AS __ts_list
    FROM _src
    GROUP BY group_col
),
_unnested AS (
    SELECT
        __ts_g,
        UNNEST(range(1, len(__ts_list) + 1)) AS __ts_rn2,
        UNNEST(__ts_list) AS __ts_v
    FROM _filled
)
SELECT _src.* EXCLUDE (__ts_rn), __ts_v AS filled_value
FROM _src
JOIN _unnested ON group_col IS NOT DISTINCT FROM __ts_g AND __ts_rn = __ts_rn2
ORDER BY group_col, date_col
)",
    "Fills NULL values in each group's time series with the group mean, computed in Rust "
    "(fill_nulls_mean): NaN counts as missing; an all-NULL/all-NaN group keeps every value NULL. "
    "All input columns are preserved.",
    "SELECT * FROM ts_fill_nulls_mean_by('sales', product_id, date, qty)",
    "data-preparation"},

    // ts_diff_by: d-th order differences computed in Rust (Phase 12 D-05/D-06)
    // C++ API: ts_diff_by(table_name, group_col, date_col, value_col, diff_order, seasonal_lag := 1)
    {"ts_diff_by", {"source", "group_col", "date_col", "value_col", "diff_order", nullptr},
     {{"seasonal_lag", "1"}, {nullptr, nullptr}},
R"(
WITH _src AS (
    SELECT *, ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY date_col) AS __ts_rn
    FROM query_table(source::VARCHAR)
),
_diffed AS (
    SELECT
        group_col AS __ts_g,
        _ts_diff(LIST(value_col::DOUBLE ORDER BY __ts_rn), diff_order::INTEGER, seasonal_lag::INTEGER) AS __ts_list
    FROM _src
    GROUP BY group_col
),
_unnested AS (
    SELECT
        __ts_g,
        UNNEST(range(1, len(__ts_list) + 1)) AS __ts_rn2,
        UNNEST(__ts_list) AS __ts_v
    FROM _diffed
)
SELECT
    group_col,
    date_col,
    __ts_v AS diff_value
FROM _src
JOIN _unnested ON group_col IS NOT DISTINCT FROM __ts_g AND __ts_rn = __ts_rn2
ORDER BY group_col, date_col
)",
    "Computes d-th order differences for each group's time series in Rust (numpy diff(n=diff_order)): "
    "value[t] - value[t-1] applied diff_order times. seasonal_lag (default 1) sets the lag-k difference "
    "step for the old lag-based semantics (R diff(x, lag=seasonal_lag, differences=diff_order)). One row "
    "per input row; the first diff_order*seasonal_lag rows per group are NULL. Any difference touching a "
    "NULL input is NULL (never an error).",
    "SELECT * FROM ts_diff_by('sales', product_id, date, qty, 2) -- or: ..., 1, seasonal_lag := 2)",
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
    // The native's input is (... FROM query_table(source) ORDER BY ALL): the ORDER BY is a
    // single-pipeline breaker, so a UNION ALL view cannot hand the collect-then-finalize
    // native several pipelines that finalize on partial input (#285, Phase 20 20-03).
    {"ts_fill_gaps_by", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_fill_gaps_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL),
    frequency::VARCHAR
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
    // The native's input is (... FROM query_table(source) ORDER BY ALL): the ORDER BY is a
    // single-pipeline breaker, so a UNION ALL view cannot hand the collect-then-finalize
    // native several pipelines that finalize on partial input (#285, Phase 20 20-03).
    {"ts_fill_forward_by", {"source", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_fill_forward_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL),
    target_date,
    frequency::VARCHAR
)
)",
    "Forward-fills missing timestamps in each group's time series using the last known value.",
    "SELECT * FROM ts_fill_forward_by('sales', product_id, date, qty, '1d')",
    "data-preparation"},

    // ts_fill_forward_operator: DEPRECATED VARCHAR-column-name form of ts_fill_forward_by (GH#289)
    // C++ API: ts_fill_forward_operator(source_table, group_col, date_col, value_col, target_date, frequency)
    //
    // Previously a native C++ table function that ran a nested QueryMaterialized query from
    // InitGlobal, hung natively and trapped DuckDB-Wasm, interpolated identifiers unquoted via
    // StringUtil::Format, and ignored the frequency argument (always one day). It is now a thin
    // macro over ts_fill_forward_by: COLUMNS([param]) resolves each VARCHAR column-name argument
    // to a column reference at bind time via AST substitution, so reserved words and names with
    // spaces need no quoting and no SQL text is ever formatted from user input. Deprecated and
    // scheduled for removal in the next release — use ts_fill_forward_by with column references.
    {"ts_fill_forward_operator", {"source_table", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM ts_fill_forward_by(source_table, COLUMNS([group_col]), COLUMNS([date_col]), COLUMNS([value_col]), target_date, frequency)
)",
    "DEPRECATED (removed in the next release): use ts_fill_forward_by with column references. "
    "Takes the group/date/value columns as VARCHAR names and forward-fills each group to target_date at the given frequency.",
    "SELECT * FROM ts_fill_forward_operator('sales', 'store_id', 'sale_date', 'qty', TIMESTAMP '2024-01-10', '1d')",
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
    // The native's input is (... FROM query_table(source) ORDER BY ALL): the ORDER BY is a
    // single-pipeline breaker, so a UNION ALL view cannot hand the collect-then-finalize
    // native several pipelines that finalize on partial input (#285, Phase 20 20-03).
    {"ts_mstl_decomposition_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_mstl_decomposition_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL),
    COALESCE(json_extract_string(to_json(params), '$.insufficient_data'), 'fail'),
    CAST(COALESCE(json_extract_string(to_json(params), '$.seasonal_periods'), json_extract_string(to_json(params), '$.periods'), '[]') AS INTEGER[])
)
)",
    "Decomposes each group's time series into trend, seasonal, and remainder components using MSTL. params: seasonal_periods (alias periods) is a list of seasonal periods to extract, e.g. '[7, 365]' or [7, 365]; insufficient_data is 'fail' (default), 'trend', or 'none'.",
    "SELECT * FROM ts_mstl_decomposition_by('sales', product_id, date, qty, MAP{'seasonal_periods': '[7, 365]'})",
    "decomposition"},

    // ts_detrend_by: Remove trend from grouped time series
    // C++ API: ts_detrend_by(table_name, group_col, date_col, value_col, method)
    // method: 'linear', 'quadratic' (alias 'polynomial', 'poly'), 'cubic',
    //   'diff' (alias 'diff1', 'difference'), 'diff2', 'loess' (alias 'lowess'),
    //   'auto' (default); an unknown method raises an error naming these choices.
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
    "SELECT * FROM ts_detrend_by('sales', product_id, date, qty, 'cubic')",
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
lead_count AS (
    -- Phase 12 D-03: _ts_detect_changepoints_bocpd trims leading NULL/NaN
    -- values before analysis (plan 12-03). _lead = number of leading rows
    -- (ordered by date_col) before the first non-NULL value; if every value
    -- is NULL, _lead = row count (every row falls outside the trimmed range
    -- below, so every flag is NULL).
    SELECT COALESCE(MIN(_idx), (SELECT COUNT(*) FROM ordered_data) + 1) - 1 AS _lead
    FROM ordered_data
    WHERE _val IS NOT NULL
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
    CASE WHEN (od._idx - lc._lead) BETWEEN 1 AND len((cp.cp).is_changepoint)
         THEN (cp.cp).is_changepoint[od._idx - lc._lead]
         ELSE NULL END AS is_changepoint,
    CASE WHEN (od._idx - lc._lead) BETWEEN 1 AND len((cp.cp).changepoint_probability)
         THEN (cp.cp).changepoint_probability[od._idx - lc._lead]
         ELSE NULL END AS changepoint_probability
FROM ordered_data od, cp_result cp, lead_count lc
ORDER BY od._dt
)",
    "Detects structural changepoints in a single time series using Bayesian Online Changepoint Detection.",
    "SELECT * FROM ts_detect_changepoints('sales', product_id, date, qty)",
    "changepoint-detection"},

    // ts_detect_changepoints_by: Detect changepoints per group (row-level output)
    // C++ API: ts_detect_changepoints_by(table_name, group_col, date_col, value_col, params MAP)
    // Returns: TABLE with group_col (preserved name!), date_col, value_col, is_changepoint, changepoint_probability
    //
    // The native's input is (... FROM query_table(source) ORDER BY ALL): the ORDER BY is a
    // single-pipeline breaker, so a UNION ALL view cannot hand the collect-then-finalize
    // native several pipelines that finalize on partial input (#285, Phase 20 20-03).
    //
    // This macro wraps the native streaming implementation (_ts_detect_changepoints_by_native)
    // which fixes issue #149:
    // - Preserves the input group column name (was hardcoded as 'id')
    // - Returns row-level data with time column included
    {"ts_detect_changepoints_by", {"source", "group_col", "date_col", "value_col", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_detect_changepoints_by_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL),
    params::MAP(VARCHAR, VARCHAR)
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
               frequency::VARCHAR,
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

    // _ts_forecast_native: INTERNAL ONLY (#285, 20-05). This underscore-prefixed
    // macro wraps the renamed `_ts_forecast_native_impl` native (ts_forecast_native.cpp)
    // with a single-pipeline input, (SELECT * FROM _ts_passthrough(source) ORDER BY ALL),
    // so a UNION ALL source is read in full instead of silently truncated. It exists
    // only because test/sql/ts_forecast_param_grid.test, ts_native_model_names.test and
    // ts_native_param_validation.test call it directly, positional. ts_forecast_by does
    // NOT use this macro or the native behind it (GROUP BY + _ts_forecast_scalar; already
    // correct on UNION ALL sources, untouched here). RegisterTsTableMacros skips the
    // anofox_fcst_ alias for any name starting with '_'.
    {"_ts_forecast_native", {"source", "horizon", "frequency", "method", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_forecast_native_impl((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), horizon, frequency, method, params)
)",
    nullptr, nullptr, nullptr},

    // ts_forecast_ensemble_by: Explicit-member ensemble forecasting (Phase 5: ENS-02)
    //
    // Fits each named member model independently per series and combines forecasts
    // using the specified combination method (default: Mean). Member names follow
    // the same vocabulary as ts_forecast_by (e.g. 'AutoARIMA', 'AutoETS', 'Theta').
    //
    // Blocked members (not expressible with a single seasonal_period):
    //   GARCH, Laplace, ARIMA, MFLES, AutoMFLES, MSTL, AutoMSTL, TBATS, AutoTBATS, AutoEnsemble.
    // Combination methods: 'mean' (default), 'median', 'weighted_mse',
    //   'inverse_aic', 'stacking', 'horizon_adaptive'.
    // yhat_lower / yhat_upper are the crate Ensemble's own prediction
    // interval at confidence_level (default 0.90) — D-01, plan 10-03.
    {"ts_forecast_ensemble_by",
     {"source", "group_col", "date_col", "target_col", "members", "horizon", "frequency", nullptr},
     {{"combination_method", "''"}, {"seasonal_period", "0"}, {"confidence_level", "0.90"}, {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
FROM (
    SELECT group_col,
           unnest(_ts_forecast_ensemble_native(
               LIST(date_col ORDER BY date_col),
               LIST(target_col::DOUBLE ORDER BY date_col),
               members,
               horizon,
               frequency,
               combination_method,
               seasonal_period,
               confidence_level
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
    "Generates ensemble forecasts from explicitly named member models per series. "
    "members is a VARCHAR[] list (e.g. ['AutoARIMA','AutoETS','Naive']). "
    "combination_method: 'mean' (default), 'median', 'weighted_mse', 'inverse_aic', 'stacking', 'horizon_adaptive'. "
    "yhat_lower / yhat_upper are the crate Ensemble's own prediction interval at confidence_level (default 0.90).",
    "SELECT * FROM ts_forecast_ensemble_by('sales', product_id, date, qty, ['AutoARIMA','AutoETS','Naive'], 12, '1d')",
    "forecasting"},

    // ts_ensemble_inspect_by: Explicit-member ensemble introspection (Phase 6: INSP-01)
    //
    // Inspects per-member combination weights for an explicit-member ensemble.
    // Returns long-format rows: (group_col, member_name, weight, score) — one row per member per series.
    // weight sums to 1.0 per group for all six combination methods.
    // score column is NULL (explicit-member has no per-member MSE; use ts_auto_ensemble_inspect_by
    // for AutoEnsemble MSE scores).
    //
    // Signature: ts_ensemble_inspect_by(source, group_col, date_col, target_col, members;
    //                                    combination_method := '', seasonal_period := 0)
    {"ts_ensemble_inspect_by",
     {"source", "group_col", "date_col", "target_col", "members", nullptr},
     {{"combination_method", "''"}, {"seasonal_period", "0"}, {nullptr, nullptr}},
R"(
SELECT group_col, member_name, weight, score
FROM (
    SELECT group_col,
           unnest(_ts_ensemble_inspect_native(
               LIST(target_col::DOUBLE ORDER BY date_col),
               members,
               combination_method,
               seasonal_period
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
    "Returns per-member combination weights for an explicit-member ensemble. "
    "One row per (group, member). weight sums to 1.0 per group for all six combination methods. "
    "score column is NULL (use ts_auto_ensemble_inspect_by for AutoEnsemble in-sample MSE scores). "
    "combination_method: 'mean' (default), 'median', 'weighted_mse', 'inverse_aic', 'stacking', 'horizon_adaptive'. "
    "HorizonAdaptive returns the AVERAGE per-horizon weight (per-step matrix out of scope for INSP-01 v1).",
    "SELECT * FROM ts_ensemble_inspect_by('sales', id, ds, y, ['AutoARIMA','AutoETS','Naive'], "
    "combination_method := 'weighted_mse')",
    "forecasting"},

    // ts_auto_ensemble_inspect_by: AutoEnsemble introspection (Phase 6: INSP-01)
    //
    // Inspects selected members and in-sample MSE scores for an AutoEnsemble per series.
    // Returns long-format rows: (group_col, member_name, weight, score, rank) — one row per
    // selected member per series. rank = 1..k by ascending MSE (best = 1).
    // weight = 1/k for Mean combination; NULL for all other methods (WeightedMSE, InverseAIC,
    // Stacking, HorizonAdaptive) — crate 0.15.3 limitation: inner AutoEnsemble weights are
    // not accessible via the public API. score = in-sample MSE from AutoEnsemble.all_scores().
    //
    // Signature: ts_auto_ensemble_inspect_by(source, group_col, date_col, target_col;
    //                                         top_k := 3, combination_method := '',
    //                                         seasonal_period := 0)
    {"ts_auto_ensemble_inspect_by",
     {"source", "group_col", "date_col", "target_col", nullptr},
     {{"top_k", "3"}, {"combination_method", "''"}, {"seasonal_period", "0"}, {nullptr, nullptr}},
R"(
SELECT group_col, member_name, weight, score,
       ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY score) AS rank
FROM (
    SELECT group_col,
           unnest(_ts_auto_ensemble_inspect_native(
               LIST(target_col::DOUBLE ORDER BY date_col),
               top_k,
               combination_method,
               seasonal_period
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
    "Returns selected member names and in-sample MSE scores for an AutoEnsemble per series. "
    "weight = 1/k for Mean combination; NULL for all other methods (crate 0.15.3 limitation — "
    "inner AutoEnsemble combination weights are not accessible via public API). "
    "rank = 1..k by ascending MSE (best = 1). score = in-sample MSE from AutoEnsemble.all_scores().",
    "SELECT * FROM ts_auto_ensemble_inspect_by('sales', id, ds, y, top_k := 3, "
    "combination_method := 'mean')",
    "forecasting"},

    // ts_forecast_panel_by: Panel forecasting via cross-series global learners (Phase 2: GLOB-01..03)
    //
    // Fits a single shared-parameter model across all series simultaneously, then emits
    // one row per (series, horizon step) — the fit-once-emit-many pattern. Unlike
    // ts_forecast_by which fits independently per series, this achieves true cross-series
    // learning. Ragged panels are auto-aligned to a shared date grid.
    //
    // Signature: ts_forecast_panel_by(source, group_col, date_col, target_col, method,
    //                                  horizon, frequency, params := MAP{})
    // Supported methods: 'GlobalETS' (GlobalTheta, GlobalCroston added in 02-2)
    {"ts_forecast_panel_by", {"source", "group_col", "date_col", "target_col", "method", "horizon", "frequency", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, date_col, yhat, model_name
FROM _ts_forecast_panel_native(
    (SELECT group_col, date_col, target_col::DOUBLE FROM query_table(source::VARCHAR) ORDER BY ALL),
    horizon,
    frequency,
    method,
    params
)
)",
    "Forecasts a grouped panel using cross-series global learners (GlobalETS, GlobalTheta, GlobalCroston). "
    "All series are fitted simultaneously with shared parameters (fit-once-emit-many). "
    "Ragged panels are auto-aligned to a shared date grid before the global fit. "
    "Returns one row per (group, horizon step). "
    "Series with fewer than 10 valid observations are surfaced with model_name = 'DROPPED: too_short'.",
    "SELECT * FROM ts_forecast_panel_by('sales', product_id, date, qty, 'GlobalETS', 14, '1d', MAP{'seasonal_period': '7'})",
    "forecasting"},

    // ts_forecast_var_by: VAR multivariate forecasting (Phase 3: CLAS-03)
    //
    // Fits a single VAR(p) model across ALL K value columns simultaneously, then emits
    // one row per (variable, horizon step) in LONG format. Unlike ts_forecast_by which
    // fits independently per series, this achieves true multivariate cross-variable
    // learning via the VAR coefficient matrix.
    //
    // v1: single-panel only — no group_col. All rows in source are treated as one panel.
    // forecast_value is a point forecast (no prediction intervals in v1).
    //
    // Signature: ts_forecast_var_by(source, date_col, value_cols, horizon, frequency,
    //                                order := 1, params := MAP{})
    // output: variable VARCHAR, forecast_step BIGINT, <date_col> <date_type>, forecast_value DOUBLE
    //
    // IMPORTANT: uses the subselect pattern to avoid silent macro-registration failure
    // (Phase-2 lesson: bare query_table() TABLE arg silently produces 0 rows in duckdb_functions())
    {"ts_forecast_var_by",
     {"source", "date_col", "value_cols", "horizon", "frequency", nullptr},
     {{"p", "1"}, {"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT *
FROM _ts_forecast_var_native(
    (SELECT * FROM query_table(source::VARCHAR) ORDER BY ALL),
    horizon,
    frequency,
    p,
    value_cols,
    date_col,
    params
)
)",
    "VAR multivariate forecasting. Fits a single VAR(p) model across all K value columns and "
    "returns one row per (variable, horizon step) in long format (LONG output shape). "
    "value_cols is a VARCHAR[] of column names from source (e.g. ['y1','y2']). "
    "date_col is the name of the date column (VARCHAR string, e.g. 'ds'). "
    "p is the VAR lag order (default 1). "
    "forecast_value is a point forecast (no prediction intervals in v1). "
    "v1 is single-panel only (no group_col — one VAR fit for the entire input table).",
    "SELECT * FROM ts_forecast_var_by('returns', 'ds', ['equity','bond','fx'], 12, '1d', p:=2)",
    "forecasting"},

    // ts_forecast_inspect_by: Return per-group fit-state snapshot for Inspectable models.
    // C++ API: ts_forecast_inspect_by(source, group_col, date_col, target_col, method, params?)
    //
    // Underlying scalar returns the serde-serialised Explanation enum
    // (`{"Ets": {...}}`, `{"Arima": {...}}`, …). This macro unpacks the wire
    // JSON into a wide, uniformly-typed STRUCT — every field is nullable so
    // the shape stays stable regardless of which family fit.
    //
    // Requires the json extension for json_extract / json_keys.
    {"ts_forecast_inspect_by", {"source", "group_col", "date_col", "target_col", "method", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
WITH _raw AS (
    SELECT
        group_col,
        _ts_forecast_inspect_scalar(
            LIST(target_col::DOUBLE ORDER BY date_col),
            method,
            params
        ) AS _j
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
),
_tagged AS (
    SELECT group_col, _j,
           json_keys(_j)[1] AS _family,
           '$.' || json_keys(_j)[1] || '.' AS _base
    FROM _raw
    WHERE _j IS NOT NULL
)
SELECT
    group_col,
    struct_pack(
        model_family        := _family,
        spec                := json_extract_string(_j, _base || 'spec'),
        variant             := json_extract_string(_j, _base || 'variant'),
        backend             := json_extract_string(_j, _base || 'backend'),
        selected_config     := json_extract_string(_j, _base || 'selected_config'),
        alpha               := TRY_CAST(json_extract(_j, _base || 'alpha') AS DOUBLE),
        beta                := TRY_CAST(json_extract(_j, _base || 'beta') AS DOUBLE),
        gamma               := TRY_CAST(json_extract(_j, _base || 'gamma') AS DOUBLE),
        phi                 := TRY_CAST(json_extract(_j, _base || 'phi') AS DOUBLE),
        theta               := TRY_CAST(json_extract(_j, _base || 'theta') AS DOUBLE),
        aic                 := TRY_CAST(json_extract(_j, _base || 'aic') AS DOUBLE),
        bic                 := TRY_CAST(json_extract(_j, _base || 'bic') AS DOUBLE),
        r_squared           := TRY_CAST(json_extract(_j, _base || 'r_squared') AS DOUBLE),
        intercept           := TRY_CAST(json_extract(_j, _base || 'intercept') AS DOUBLE),
        intercept_std_error := TRY_CAST(json_extract(_j, _base || 'intercept_std_error') AS DOUBLE),
        box_cox_lambda      := TRY_CAST(json_extract(_j, _base || 'box_cox_lambda') AS DOUBLE),
        penalty             := TRY_CAST(json_extract(_j, _base || 'penalty') AS DOUBLE),
        seasonal_period     := TRY_CAST(json_extract(_j, _base || 'seasonal_period') AS BIGINT),
        seasonal_periods    := TRY_CAST(json_extract(_j, _base || 'seasonal_periods') AS BIGINT[]),
        iterations          := TRY_CAST(json_extract(_j, _base || 'iterations') AS BIGINT),
        max_rounds          := TRY_CAST(json_extract(_j, _base || 'max_rounds') AS BIGINT),
        multiplicative      := TRY_CAST(json_extract(_j, _base || 'multiplicative') AS BOOLEAN),
        order_p             := TRY_CAST(json_extract(_j, _base || 'order[0]') AS BIGINT),
        order_d             := TRY_CAST(json_extract(_j, _base || 'order[1]') AS BIGINT),
        order_q             := TRY_CAST(json_extract(_j, _base || 'order[2]') AS BIGINT),
        seasonal_order_P    := TRY_CAST(json_extract(_j, _base || 'seasonal_order[0]') AS BIGINT),
        seasonal_order_D    := TRY_CAST(json_extract(_j, _base || 'seasonal_order[1]') AS BIGINT),
        seasonal_order_Q    := TRY_CAST(json_extract(_j, _base || 'seasonal_order[2]') AS BIGINT),
        seasonal_order_s    := TRY_CAST(json_extract(_j, _base || 'seasonal_order[3]') AS BIGINT),
        coefficients        := TRY_CAST(json_extract(_j, _base || 'coefficients') AS DOUBLE[]),
        coef_std_errors     := TRY_CAST(json_extract(_j, _base || 'coef_std_errors') AS DOUBLE[]),
        feature_names       := TRY_CAST(json_extract(_j, _base || 'feature_names') AS VARCHAR[]),
        leaf_weights        := TRY_CAST(json_extract(_j, _base || 'leaf_weights') AS DOUBLE[]),
        leaf_names          := TRY_CAST(json_extract(_j, _base || 'leaf_names') AS VARCHAR[]),
        fitted_values       := TRY_CAST(json_extract(_j, _base || 'fitted_values') AS DOUBLE[]),
        residuals           := TRY_CAST(json_extract(_j, _base || 'residuals') AS DOUBLE[]),
        trend_component     := TRY_CAST(json_extract(_j, _base || 'trend_component') AS DOUBLE[]),
        seasonal_component  := TRY_CAST(json_extract(_j, _base || 'seasonal_component') AS DOUBLE[]),
        horizon_dists_json  := CAST(json_extract(_j, _base || 'horizon_dists') AS VARCHAR),
        raw_json            := _j
    ) AS inspection
FROM _tagged
)",
    "Returns per-group Inspectable fit-state snapshot (coefficients, AIC/BIC, components, residuals) as a wide STRUCT.",
    "SELECT group_col, (inspection).spec, (inspection).aic FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'AutoETS')",
    "forecasting"},

    // ts_forecast_explain_by: Return per-group per-horizon forecast decomposition.
    // C++ API: ts_forecast_explain_by(source, group_col, date_col, target_col, method, horizon, params?)
    //
    // Underlying scalar returns JSON with `level`, `trend`, `seasonal`,
    // `residual` (each a DOUBLE[] of length `horizon`, absent when a family
    // doesn't produce it) and `named_components` (a JSON object of extra
    // additive contributions).
    //
    // Only ETS (fixed spec), MSTL / AutoMSTL, and Theta implement Explainable;
    // other models return an InvalidModel error from the underlying scalar.
    //
    // Requires the json extension for json_extract.
    {"ts_forecast_explain_by", {"source", "group_col", "date_col", "target_col", "method", "horizon", nullptr}, {{"params", "MAP{}"}, {nullptr, nullptr}},
R"(
WITH _raw AS (
    SELECT
        group_col,
        _ts_forecast_explain_scalar(
            LIST(target_col::DOUBLE ORDER BY date_col),
            horizon,
            method,
            params
        ) AS _j
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
SELECT
    group_col,
    struct_pack(
        horizon                := horizon,
        level                  := TRY_CAST(json_extract(_j, '$.level') AS DOUBLE[]),
        trend                  := TRY_CAST(json_extract(_j, '$.trend') AS DOUBLE[]),
        seasonal               := TRY_CAST(json_extract(_j, '$.seasonal') AS DOUBLE[]),
        residual               := TRY_CAST(json_extract(_j, '$.residual') AS DOUBLE[]),
        named_components_json  := CAST(json_extract(_j, '$.named_components') AS VARCHAR),
        raw_json               := _j
    ) AS decomposition
FROM _raw
WHERE _j IS NOT NULL
)",
    "Returns per-horizon forecast decomposition (level, trend, seasonal, residual) per group as a wide STRUCT. Supports ETS, MSTL, Theta.",
    "SELECT group_col, (decomposition).trend, (decomposition).seasonal FROM ts_forecast_explain_by('sales', product_id, ds, y, 'ETS', 12, {seasonal_period: 12})",
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
     FROM query_table(ml_folds::VARCHAR) ORDER BY ALL),
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
        method,
        params
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
        LIST(__exog_dt__::TIMESTAMP ORDER BY __exog_dt__) AS _dates,
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
        h._dates,
        _ts_forecast_exog(
            h._y_list,
            COALESCE(x._xreg_list, []::DOUBLE[][]),
            COALESCE(fx._future_xreg_list, []::DOUBLE[][]),
            horizon,
            method,
            params
        ) AS fcst
    FROM grouped_historical h
    LEFT JOIN _xreg_lists x ON h.__exog_grp__ = x.__exog_grp__
    LEFT JOIN _future_xreg_lists fx ON h.__exog_grp__ = fx.__exog_fgrp__
)
SELECT
    __exog_grp__ AS id,
    UNNEST(generate_series(1, len((fcst).point)))::INTEGER AS forecast_step,
    UNNEST(_ts_future_dates(_dates, len((fcst).point)::INTEGER, (SELECT _interval FROM _freq))) AS date,
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
    (SELECT group_col, date_col, target_col FROM query_table(source::VARCHAR) ORDER BY ALL),
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
     FROM query_table(source::VARCHAR) ORDER BY ALL),
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
    (SELECT * FROM joined ORDER BY ALL),
    unknown_features,
    params
)
)",
    "Joins external features to CV splits with proper masking to prevent data leakage.",
    "SELECT * FROM ts_cv_hydrate_by('cv_splits', product_id, date, qty, 'features', unknown_cols)",
    "cross-validation"},

    // ts_conformal_by: Compute conformal prediction intervals, one row per (group, forecast row)
    // C++ API: ts_conformal_by(backtest_results, group_col, date_col, actual_col, forecast_col, point_forecast_col, params := MAP{})
    // backtest_results: table holding BOTH calibration rows (actual_col/forecast_col set,
    //   point_forecast_col NULL) and future rows (actual_col/forecast_col NULL, point_forecast_col set),
    //   all ordered/paired by date_col (Phase 16 D-01). Residuals and empirical coverage are
    //   computed in Rust, never subtracted in SQL (D-02).
    // params MAP supports (read json-free; CAST, not TRY_CAST, so a malformed value errors):
    //   alpha (DOUBLE) - miscoverage rate (default 0.1 for 90% coverage)
    //   method (VARCHAR) - 'symmetric' (alias 'split'), 'asymmetric', or 'adaptive' (default 'symmetric')
    // difficulty_col (Phase 16 D-08, plan 16-02): named, default NULL. Carried as `d` in the
    // same date-ordered STRUCT lists as actual/forecast/point; only method 'adaptive' may use
    // it (any present value with another method is an SQL error).
    // Returns: group_col, date_col, point, lower, upper, coverage, nominal_level,
    //          conformity_score, n_calibration, calibration_insufficient, method
    {"ts_conformal_by", {"backtest_results", "group_col", "date_col", "actual_col", "forecast_col", "point_forecast_col", nullptr}, {{"params", "MAP{}"}, {"difficulty_col", "NULL"}, {nullptr, nullptr}},
R"(
WITH _params AS (
    SELECT
        CAST(COALESCE((params::MAP(VARCHAR, VARCHAR))['alpha'], '0.1') AS DOUBLE) AS _alpha,
        COALESCE((params::MAP(VARCHAR, VARCHAR))['method'], 'symmetric') AS _method
),
per_group AS (
    SELECT
        group_col AS _grp,
        LIST({a: actual_col::DOUBLE, f: forecast_col::DOUBLE, d: difficulty_col::DOUBLE} ORDER BY date_col) AS _pairs,
        LIST({t: date_col, p: point_forecast_col::DOUBLE, d: difficulty_col::DOUBLE} ORDER BY date_col)
            FILTER (WHERE point_forecast_col IS NOT NULL) AS _points
    FROM query_table(backtest_results::VARCHAR)
    GROUP BY group_col
),
conformal_calc AS (
    SELECT
        _grp,
        _points,
        _ts_conformal_from_pairs(
            [pr.a FOR pr IN _pairs],
            [pr.f FOR pr IN _pairs],
            [pr.d FOR pr IN _pairs],
            [pt.p FOR pt IN _points],
            [pt.d FOR pt IN _points],
            (SELECT _alpha FROM _params),
            (SELECT _method FROM _params)
        ) AS conf_result
    FROM per_group
    WHERE len(_points) > 0
)
SELECT
    _grp AS group_col,
    UNNEST(_points).t AS date_col,
    UNNEST((conf_result).point) AS point,
    UNNEST((conf_result).lower) AS lower,
    UNNEST((conf_result).upper) AS upper,
    (conf_result).coverage AS coverage,
    (conf_result).nominal_level AS nominal_level,
    (conf_result).conformity_score AS conformity_score,
    (conf_result).n_calibration AS n_calibration,
    (conf_result).calibration_insufficient AS calibration_insufficient,
    (conf_result).method AS method
FROM conformal_calc
)",
    "One-step conformal prediction: calibrates on backtest residuals and applies to new forecasts, one row per (group, date).",
    "SELECT * FROM ts_conformal_by('backtest', product_id, date, actual, forecast, point_forecast)",
    "conformal-prediction"},

    // ts_conformal_calibrate: Compute conformal quantile from backtest residuals
    // C++ API: ts_conformal_calibrate(backtest_results, actual_col, forecast_col, params)
    // Returns a single row with the calibrated conformity score
    // params MAP supports:
    //   alpha (DOUBLE) - miscoverage rate (default 0.1 for 90% coverage)
    // ts_conformal_calibrate: Calibrate a conformal predictor PER GROUP by default (Phase 16 D-03)
    // C++ API: ts_conformal_calibrate(backtest_results, group_col, actual_col, forecast_col, params := MAP{})
    // Residuals are computed in Rust (D-02), never subtracted in SQL. No date column: the split
    // quantile is order-invariant over calibration pairs (D-01 names only the three macros that
    // order/pair forecasts; calibration pairing is row-inherent, one STRUCT per row).
    // params MAP supports (read json-free; CAST, not TRY_CAST):
    //   alpha (DOUBLE) - miscoverage rate (default 0.1)
    //   method (VARCHAR) - 'symmetric' (alias 'split') or 'adaptive' (default 'symmetric';
    //     'asymmetric' errors -- two margins cannot collapse into one conformity_score, use
    //     ts_conformal_by instead)
    //   pooled (BOOLEAN) - false (default): one row per group_col value. true: pool every row
    //     into ONE row with group_col = NULL.
    // difficulty_col (Phase 16 D-08, plan 16-02): named, default NULL. Only method 'adaptive'
    // may use it.
    // Returns: group_col, method, conformity_score, coverage, nominal_level, n_calibration,
    //          calibration_insufficient
    {"ts_conformal_calibrate", {"backtest_results", "group_col", "actual_col", "forecast_col", nullptr}, {{"params", "MAP{}"}, {"difficulty_col", "NULL"}, {nullptr, nullptr}},
R"(
WITH _params AS (
    SELECT
        CAST(COALESCE((params::MAP(VARCHAR, VARCHAR))['alpha'], '0.1') AS DOUBLE) AS _alpha,
        COALESCE((params::MAP(VARCHAR, VARCHAR))['method'], 'symmetric') AS _method,
        CAST(COALESCE((params::MAP(VARCHAR, VARCHAR))['pooled'], 'false') AS BOOLEAN) AS _pooled
),
per_key AS (
    SELECT
        CASE WHEN (SELECT _pooled FROM _params) THEN NULL ELSE group_col END AS _grp,
        LIST({a: actual_col::DOUBLE, f: forecast_col::DOUBLE, d: difficulty_col::DOUBLE}) AS _pairs
    FROM query_table(backtest_results::VARCHAR)
    GROUP BY CASE WHEN (SELECT _pooled FROM _params) THEN NULL ELSE group_col END
),
calibrated AS (
    SELECT
        _grp,
        _ts_conformal_calibrate(
            [pr.a FOR pr IN _pairs],
            [pr.f FOR pr IN _pairs],
            [pr.d FOR pr IN _pairs],
            (SELECT _alpha FROM _params),
            (SELECT _method FROM _params)
        ) AS cal_result
    FROM per_key
)
SELECT
    _grp AS group_col,
    (cal_result).method AS method,
    (cal_result).conformity_score AS conformity_score,
    (cal_result).coverage AS coverage,
    (cal_result).nominal_level AS nominal_level,
    (cal_result).n_calibration AS n_calibration,
    (cal_result).calibration_insufficient AS calibration_insufficient
FROM calibrated
)",
    "Calibrates a conformal predictor per group (or pooled on request) and returns the calibration profile for reuse.",
    "SELECT * FROM ts_conformal_calibrate('backtest', product_id, actual, forecast)",
    "conformal-prediction"},

    // ts_conformal_apply_by: Apply a calibration TABLE (ts_conformal_calibrate output) to new
    // forecasts, one row per (group, forecast row) (Phase 16 D-01/D-03/D-14)
    // C++ API: ts_conformal_apply_by(forecast_results, group_col, date_col, forecast_col, calibration)
    // `calibration` names a table holding ts_conformal_calibrate's output columns (group_col,
    // method, conformity_score); it must hold at MOST one row per group_col value. Per forecast
    // group: the calibration row whose group_col matches is used if one exists, otherwise bounds
    // come back NULL for that group's rows.
    // Phase 16 WR-03 fix: a NULL group_col row is treated as the pooled fallback ONLY when it is
    // the SOLE row of the calibration table -- exactly the shape ts_conformal_calibrate(params :=
    // {'pooled': true}) and the literal-score one-row wrapper (D-14) both produce. A per-group
    // calibration table that happens to also contain a row with a genuine NULL group_col (e.g. an
    // ungrouped segment in the caller's own backtest data) no longer silently applies that row's
    // score to every other, unrelated group -- it is just an unmatched extra row.
    // Phase 16 WR-04 fix: more than one calibration row sharing the same group_col value
    // (including more than one row with group_col = NULL) raises a clear SQL error instead of
    // silently fanning the join out into duplicate output rows per forecast point.
    // difficulty_col (Phase 16 D-08, plan 16-02): named, default NULL. Bounds are computed via
    // ts_conformal_intervals(points, conformity_score, method, difficulties) using the matched
    // calibration row's OWN method, so the method/difficulty strictness rules (difficulty only
    // for 'adaptive', 'adaptive' requires difficulty) are enforced in Rust, not duplicated here.
    // Returns: group_col, date_col, point, lower, upper, method, conformity_score
    {"ts_conformal_apply_by", {"forecast_results", "group_col", "date_col", "forecast_col", "calibration", nullptr}, {{"difficulty_col", "NULL"}, {nullptr, nullptr}},
R"(
WITH cal AS (
    SELECT * FROM query_table(calibration::VARCHAR)
),
cal_dupe_check AS (
    -- NOTE: `cal.group_col` must stay qualified (never a bare `group_col`)
    -- here -- a bare `group_col` is textually identical to this macro's own
    -- `group_col` parameter name and would be substituted with the caller's
    -- argument (e.g. a plain column name like `g`), breaking the binder.
    SELECT CASE WHEN COUNT(*) > 0 THEN
        error('ts_conformal_apply_by: calibration table has more than one row for the same group_col value (it must hold at most one row per group_col value, including at most one pooled row with group_col = NULL)')
    ELSE NULL END AS _err
    FROM (SELECT cal.group_col FROM cal GROUP BY cal.group_col HAVING COUNT(*) > 1) _dupes
),
cal_pooled_eligible AS (
    SELECT (COUNT(*) = 1) AS _eligible FROM cal
),
per_group AS (
    SELECT
        group_col AS _grp,
        LIST({t: date_col, p: forecast_col::DOUBLE, d: difficulty_col::DOUBLE} ORDER BY date_col)
            FILTER (WHERE forecast_col IS NOT NULL) AS _points
    FROM query_table(forecast_results::VARCHAR)
    GROUP BY group_col
),
matched AS (
    SELECT
        pg._grp AS _grp,
        pg._points AS _points,
        COALESCE(direct.conformity_score, pooled.conformity_score, dupe_check._err) AS _score,
        COALESCE(direct.method, pooled.method) AS _method
    FROM per_group pg
    CROSS JOIN cal_pooled_eligible eligible
    CROSS JOIN cal_dupe_check dupe_check
    LEFT JOIN cal direct ON direct.group_col = pg._grp
    LEFT JOIN cal pooled ON pooled.group_col IS NULL AND eligible._eligible
),
applied AS (
    SELECT
        _grp,
        _points,
        _method,
        _score,
        ts_conformal_intervals([pt.p FOR pt IN _points], _score, _method, [pt.d FOR pt IN _points]) AS _bounds
    FROM matched
)
SELECT
    _grp AS group_col,
    UNNEST(_points).t AS date_col,
    UNNEST(_points).p AS point,
    UNNEST((_bounds).lower) AS lower,
    UNNEST((_bounds).upper) AS upper,
    _method AS method,
    _score AS conformity_score
FROM applied
)",
    "Applies a calibration table (from ts_conformal_calibrate) to new point forecasts, matching each group to its own row or a pooled row.",
    "SELECT * FROM ts_conformal_apply_by('forecasts', product_id, date, point_forecast, 'calibration_profile')",
    "conformal-prediction"},

    // ts_interval_width_by: Compute mean interval width for grouped series, pairing lower/upper
    // by date_col (Phase 16 D-01/D5) -- lower and upper are read from ONE date-ordered LIST of
    // per-row STRUCTs, never sorted independently, and Rust drops incomplete (lower, upper)
    // pairs pairwise (ts_mean_interval_width's existing complete_cases behavior).
    // C++ API: ts_interval_width_by(results, group_col, date_col, lower_col, upper_col)
    // Returns: group_col, mean_width, n_intervals
    {"ts_interval_width_by", {"results", "group_col", "date_col", "lower_col", "upper_col", nullptr}, {{nullptr, nullptr}},
R"(
WITH per_group AS (
    SELECT
        group_col AS _grp,
        LIST({l: lower_col::DOUBLE, u: upper_col::DOUBLE} ORDER BY date_col) AS _pairs,
        COUNT(*) FILTER (WHERE lower_col IS NOT NULL AND upper_col IS NOT NULL) AS _n_intervals
    FROM query_table(results::VARCHAR)
    GROUP BY group_col
)
SELECT
    _grp AS group_col,
    ts_mean_interval_width(
        [pr.l FOR pr IN _pairs],
        [pr.u FOR pr IN _pairs]
    ) AS mean_width,
    _n_intervals::BIGINT AS n_intervals
FROM per_group
)",
    "Computes the mean width of prediction intervals for each group, pairing lower/upper by date_col.",
    "SELECT * FROM ts_interval_width_by('forecasts', product_id, date, lower, upper)",
    "conformal-prediction"},

    // ================================================================================
    // Multi-key unique_id functions (Issue #78)
    // ================================================================================
    // NOTE: ts_combine_keys is still a native table function (streaming, no
    // in_out_function_final, so it is not affected by #285).
    // ts_validate_separator, ts_aggregate_hierarchy and ts_split_keys are
    // collect-then-finalize natives and were silently truncating a UNION ALL
    // source (#285, decisions_addendum_2, 20-05). Each is now a table macro
    // over a renamed `_ts_*_native` (ts_validate_separator.cpp,
    // ts_aggregate_hierarchy.cpp, ts_split_keys.cpp) that reads its source
    // through (SELECT * FROM _ts_passthrough(source) ORDER BY ALL), the same
    // single-pipeline breaker the metric/detection/CV macros use. Call
    // signatures and output columns are unchanged.

    // ts_aggregate_hierarchy: aggregate a leaf-level time series up through a
    // hierarchy of group keys. `params` stays a required positional MAP (no
    // default, matching the pre-20-05 native's signature).
    {"ts_aggregate_hierarchy", {"source", "params", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_aggregate_hierarchy_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), params)
)",
    "Aggregates time series data up through a hierarchy of group keys, rolling up lower-level series to higher levels.",
    "SELECT * FROM ts_aggregate_hierarchy(TABLE(SELECT date, value, region, store, item FROM t), MAP {'separator': '|'})",
    "hierarchical"},

    // ts_split_keys: split a composite unique_id column back into its
    // component key columns. `columns` defaults to NULL (auto-generated
    // id_part_N names), matching the pre-20-05 native's optional parameter.
    {"ts_split_keys", {"source", nullptr}, {{"separator", "'|'"}, {"columns", "NULL"}, {nullptr, nullptr}},
R"(
SELECT * FROM _ts_split_keys_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), separator := separator, columns := columns)
)",
    "Splits a composite group key column back into its original multiple key columns.",
    "SELECT * FROM ts_split_keys(TABLE(SELECT unique_id, date, value FROM t), separator='|', columns=['region', 'store', 'item'])",
    "hierarchical"},

    // ts_validate_separator: check whether a separator is safe to use (does
    // not appear in any ID column value) across the whole source.
    {"ts_validate_separator", {"source", nullptr}, {{"separator", "'|'"}, {nullptr, nullptr}},
R"(
SELECT * FROM _ts_validate_separator_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), separator := separator)
)",
    "Validates that group key separators in composite keys are consistent and non-ambiguous.",
    "SELECT * FROM ts_validate_separator(TABLE(SELECT region, store FROM t), separator='|')",
    "utilities"},

    // ts_stats_by: Compute time series statistics grouped by a unique ID column
    // C++ API: ts_stats_by(table_name, group_col, date_col, value_col, frequency, seasonal_period := 0)
    // Returns: TABLE with group column (preserving original name!) and 36 statistics columns
    //
    // The native's input is (... FROM query_table(source) ORDER BY ALL): the ORDER BY is a
    // single-pipeline breaker, so a UNION ALL view cannot hand the collect-then-finalize
    // native several pipelines that finalize on partial input (#285, Phase 20 20-03).
    //
    // This macro is a thin wrapper around the native streaming implementation (_ts_stats_by_native)
    // which fixes the following bugs from issue #147:
    // - Bug 1: Preserves the input group column name (was hardcoded as 'id')
    // - Bug 2: Correctly calculates expected_length for monthly/quarterly/yearly frequencies
    // - Bug 3: Correctly detects gaps for calendar frequencies
    // seasonal_period (D-02, Phase 13 plan 13-02): 0 auto-detects, 1 means non-seasonal,
    // >= 2 is used explicitly for the STL-based trend_strength/seasonality_strength.
    {"ts_stats_by", {"source", "group_col", "date_col", "value_col", "frequency", nullptr}, {{"seasonal_period", "0"}, {nullptr, nullptr}},
R"(
SELECT * FROM _ts_stats_by_native(
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL),
    frequency,
    seasonal_period::INTEGER
)
)",
    "Alias for ts_stats. Computes 36 time series statistics per group (including n_gap_runs). seasonal_period (default 0) selects 0=auto-detect, 1=non-seasonal, >=2=explicit period for the STL-based trend_strength/seasonality_strength.",
    "SELECT * FROM ts_stats_by('sales', product_id, date, qty, '1d', seasonal_period := 12)",
    "statistics"},

    // ts_data_quality_by: Alias for ts_data_quality (for API consistency with _by naming pattern)
    // C++ API: ts_data_quality_by(table_name, unique_id_col, date_col, value_col, n_short, frequency)
    // Returns: TABLE with expanded quality columns
    {"ts_data_quality_by", {"source", "unique_id_col", "date_col", "value_col", "n_short", "frequency", nullptr}, {{nullptr, nullptr}},
R"(
WITH quality_data AS (
    SELECT
        unique_id_col AS unique_id,
        _ts_data_quality(
            -- CR-02 fix: drop NULL-dated rows (value+date together) before they reach
            -- the scalar function, matching ts_stats_by's/the aggregates' "drop this
            -- row" semantics for a NULL date_col, instead of aborting the whole query.
            LIST(value_col::DOUBLE ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            LIST(date_col::TIMESTAMP ORDER BY date_col) FILTER (WHERE date_col IS NOT NULL),
            frequency::VARCHAR,
            n_short::INTEGER
        ) AS _quality
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
    (_quality).n_gap_runs AS n_gap_runs,
    (_quality).n_missing AS n_missing,
    (_quality).is_constant AS is_constant,
    (_quality).is_short AS is_short
FROM quality_data
)",
    "Alias for ts_data_quality. Assesses time series data quality per group (dates/frequency-aware).",
    "SELECT * FROM ts_data_quality_by('sales', product_id, date, qty, 10, '1d')",
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
    (_feat).autocorrelation_lag10 AS autocorrelation_lag10,
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
    (SELECT group_col, date_col, value_col FROM query_table(source::VARCHAR) ORDER BY ALL)
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
            COALESCE(CAST(json_extract_string(to_json(params), '$.expected_periods') AS DOUBLE[]), NULL),
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
            COALESCE(CAST(json_extract_string(to_json(params), '$.expected_periods') AS DOUBLE[]), NULL),
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
SELECT * FROM _ts_metrics_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, 'mae')
)",
    "Computes Mean Absolute Error between actual and forecast columns per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_mae_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')",
    "metrics"},

    // ts_mse_by: Compute Mean Squared Error grouped by selected columns
    // C++ API: ts_mse_by(source, date_col, actual_col, forecast_col)
    {"ts_mse_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, 'mse')
)",
    "Computes Mean Squared Error between actual and forecast columns per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_mse_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')",
    "metrics"},

    // ts_rmse_by: Compute Root Mean Squared Error grouped by selected columns
    // C++ API: ts_rmse_by(source, date_col, actual_col, forecast_col)
    {"ts_rmse_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, 'rmse')
)",
    "Computes Root Mean Squared Error between actual and forecast columns per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_rmse_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')",
    "metrics"},

    // ts_mape_by: Compute Mean Absolute Percentage Error grouped by selected columns
    // C++ API: ts_mape_by(source, date_col, actual_col, forecast_col)
    {"ts_mape_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, 'mape')
)",
    "Computes Mean Absolute Percentage Error between actual and forecast columns per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_mape_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')",
    "metrics"},

    // ts_smape_by: Compute Symmetric Mean Absolute Percentage Error grouped by selected columns
    // C++ API: ts_smape_by(source, date_col, actual_col, forecast_col)
    {"ts_smape_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, 'smape')
)",
    "Computes Symmetric Mean Absolute Percentage Error between actual and forecast columns per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_smape_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')",
    "metrics"},

    // ts_r2_by: Compute R-squared (coefficient of determination) grouped by selected columns
    // C++ API: ts_r2_by(source, date_col, actual_col, forecast_col)
    {"ts_r2_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, 'r2')
)",
    "Computes R-squared (coefficient of determination) between actual and forecast columns per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_r2_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')",
    "metrics"},

    // ts_bias_by: Compute bias (mean error) grouped by selected columns
    // C++ API: ts_bias_by(source, date_col, actual_col, forecast_col)
    {"ts_bias_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_metrics_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, 'bias')
)",
    "Computes mean forecast bias between actual and forecast columns per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_bias_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast')",
    "metrics"},

    // ts_mase_by: Compute Hyndman-Koehler Mean Absolute Scaled Error with GROUP BY ALL (D-08)
    // C++ API: ts_mase_by(source, date_col, actual_col, forecast_col, seasonal_period := 1)
    // Source must include each group's history rows (date < first forecast date,
    // forecast NULL) -- they supply the in-sample seasonal-naive scale. The old
    // 5th-argument baseline-column form was removed in v2026.10.16 (see ts_rmae_by).
    {"ts_mase_by", {"source", "date_col", "actual_col", "forecast_col", nullptr}, {{"seasonal_period", "1"}, {nullptr, nullptr}},
R"(
SELECT * FROM _ts_mase_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, seasonal_period)
)",
    "Computes Hyndman-Koehler Mean Absolute Scaled Error (MASE) per group: the out-of-sample forecast MAE scaled by the in-sample seasonal-naive MAE at lag seasonal_period (default 1). Source rows dated before each group's first forecast (forecast NULL) supply the in-sample scale. The old baseline-column form was removed in v2026.10.16 -- use ts_rmae_by for a relative-MAE comparison. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_mase_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast', seasonal_period := 4)",
    "metrics"},

    // ts_rmae_by: Compute Relative Mean Absolute Error with GROUP BY ALL
    // C++ API: ts_rmae_by(source, date_col, actual_col, pred1_col, pred2_col)
    {"ts_rmae_by", {"source", "date_col", "actual_col", "pred1_col", "pred2_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_rmae_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, pred1_col, pred2_col)
)",
    "Computes Relative MAE (MAE ratio between two models) per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/pred1_col/pred2_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_rmae_by((SELECT product_id, date, actual, pred1, pred2 FROM results), 'date', 'actual', 'pred1', 'pred2')",
    "metrics"},

    // ts_coverage_by: Compute prediction interval coverage with GROUP BY ALL
    // C++ API: ts_coverage_by(source, date_col, actual_col, lower_col, upper_col)
    {"ts_coverage_by", {"source", "date_col", "actual_col", "lower_col", "upper_col", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_coverage_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, lower_col, upper_col)
)",
    "Computes prediction interval coverage rate per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/lower_col/upper_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_coverage_by((SELECT product_id, date, actual, lower, upper FROM results), 'date', 'actual', 'lower', 'upper')",
    "metrics"},

    // ts_quantile_loss_by: Compute quantile loss with GROUP BY ALL
    // C++ API: ts_quantile_loss_by(source, date_col, actual_col, forecast_col, quantile)
    {"ts_quantile_loss_by", {"source", "date_col", "actual_col", "forecast_col", "quantile", nullptr}, {{nullptr, nullptr}},
R"(
SELECT * FROM _ts_quantile_loss_native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), date_col, actual_col, forecast_col, quantile)
)",
    "Computes quantile (pinball) loss at a specified quantile level per group. "
    "source must be a subquery, e.g. (SELECT ...) -- a bare table name fails to bind "
    "(the first native-function parameter is TABLE-typed); date_col/actual_col/forecast_col "
    "are quoted column-name string literals.",
    "SELECT * FROM ts_quantile_loss_by((SELECT product_id, date, actual, forecast FROM results), 'date', 'actual', 'forecast', 0.9)",
    "metrics"},

    // ================================================================================
    // Diagnostic macros (Phase 1: STAT-01 ADF — plans 01-2/01-3 extend this section)
    // ================================================================================

    // ts_adf_by: ADF stationarity test per group (STAT-01)
    // C++ API: ts_adf_by(source, group_col, date_col, value_col [, max_lags:=-1] [, regression:='c'])
    // Returns: TABLE(group_col, adf STRUCT(statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct))
    {"ts_adf_by", {"source", "group_col", "date_col", "value_col", nullptr},
     {{"max_lags", "-1"}, {"regression", "'c'"}, {nullptr, nullptr}},
R"(
SELECT group_col, ts_adf(LIST(value_col::DOUBLE ORDER BY date_col), max_lags::INTEGER, regression::VARCHAR) AS adf
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "ADF stationarity test per group. Returns STRUCT(statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct) per group. "
    "AIC lag selection; pass max_lags:=N to override automatic lag selection. "
    "regression:='c' (constant, default) or regression:='ct' (constant + trend).",
    "SELECT group_col, (adf).statistic, (adf).p_value FROM ts_adf_by('sales', product_id, ds, y, regression := 'ct')",
    "diagnostics"},

    // ts_kpss_by: KPSS stationarity test per group (STAT-02, D-05a)
    // C++ API: ts_kpss_by(source, group_col, date_col, value_col [, lags:=-1] [, regression:='c'])
    // Returns: TABLE(group_col, kpss STRUCT(statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct))
    {"ts_kpss_by", {"source", "group_col", "date_col", "value_col", nullptr},
     {{"lags", "-1"}, {"regression", "'c'"}, {nullptr, nullptr}},
R"(
SELECT group_col, ts_kpss(LIST(value_col::DOUBLE ORDER BY date_col), lags::INTEGER, regression::VARCHAR) AS kpss
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "KPSS stationarity test per group. Returns STRUCT(statistic, p_value, lags, is_stationary, cv_1pct, cv_5pct, cv_10pct) per group. "
    "Pass lags:=N to override the automatic bandwidth. "
    "regression:='c' (level, null hypothesis is level-stationarity, default) or regression:='ct' (trend). "
    "is_stationary=true means the null is not rejected at 5%.",
    "SELECT group_col, (kpss).statistic, (kpss).is_stationary FROM ts_kpss_by('sales', product_id, ds, y, regression := 'ct')",
    "diagnostics"},

    // ts_stationarity_by: combined ADF + KPSS D-04 four-way verdict per group (STAT-03, D-04, D-05)
    // C++ API: ts_stationarity_by(source, group_col, date_col, value_col [, regression:='c'])
    // Returns: TABLE(group_col, stationarity STRUCT(adf_statistic, adf_p_value, kpss_statistic, kpss_p_value, adf_is_stationary, kpss_is_stationary, verdict))
    {"ts_stationarity_by", {"source", "group_col", "date_col", "value_col", nullptr},
     {{"regression", "'c'"}, {nullptr, nullptr}},
R"(
SELECT group_col, ts_stationarity(LIST(value_col::DOUBLE ORDER BY date_col), regression::VARCHAR) AS stationarity
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "Combined ADF + KPSS stationarity verdict per group (D-04). Returns STRUCT(adf_statistic, adf_p_value, kpss_statistic, kpss_p_value, "
    "adf_is_stationary, kpss_is_stationary, verdict) per group. verdict is one of "
    "'stationary', 'non_stationary', 'trend_stationary', 'difference_stationary', 'constant', or NULL (degenerate series). "
    "regression:='c' (default, applies to both tests) or regression:='ct'.",
    "SELECT group_col, (stationarity).verdict FROM ts_stationarity_by('sales', product_id, ds, y, regression := 'ct')",
    "diagnostics"},

    // ts_ljung_box_by: Ljung-Box residual white-noise test per group (RESID-01, D-06/DIAG-04)
    {"ts_ljung_box_by", {"source", "group_col", "date_col", "value_col", nullptr},
     {{"lags", "-1"}, {"fitted_params", "0"}, {nullptr, nullptr}},
R"(
SELECT group_col, ts_ljung_box(LIST(value_col::DOUBLE ORDER BY date_col), lags::INTEGER, fitted_params::INTEGER) AS ljung_box
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "Ljung-Box white-noise test on residuals per group. Returns STRUCT(statistic, p_value, lags, df). "
    "value_col should be model residuals; default lags = min(10, n/5). Pass lags:=N to override. "
    "df = lags - fitted_params; pass fitted_params := p + q for ARMA(p, q) residuals to get "
    "R Box.test(fitdf = p + q)-matching p-values (fitted_params >= the effective lags is a SQL error).",
    "SELECT group_col, (ljung_box).p_value FROM ts_ljung_box_by('resids', product_id, ds, e, fitted_params := 2)",
    "diagnostics"},

    // ts_durbin_watson_by: Durbin-Watson statistic per group (RESID-02)
    {"ts_durbin_watson_by", {"source", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
R"(
SELECT group_col, ts_durbin_watson(LIST(value_col::DOUBLE ORDER BY date_col)) AS durbin_watson
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "Durbin-Watson first-order autocorrelation statistic on residuals per group. "
    "Returns STRUCT(statistic, interpretation, p_value, method); statistic in [0,4], ~2 means no "
    "autocorrelation. p_value is an approximate two-sided p-value (normal approximation, as R "
    "lmtest::dwtest(exact = FALSE)); method = 'Durbin-Watson'.",
    "SELECT group_col, (durbin_watson).statistic FROM ts_durbin_watson_by('resids', product_id, ds, e)",
    "diagnostics"},

    // ts_jarque_bera_by: Jarque-Bera normality test per group (RESID-03)
    {"ts_jarque_bera_by", {"source", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
R"(
SELECT group_col, ts_jarque_bera(LIST(value_col::DOUBLE ORDER BY date_col)) AS jarque_bera
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "Jarque-Bera normality test on residuals per group. Returns STRUCT(statistic, p_value, skewness, excess_kurtosis). "
    "A small p-value rejects normality.",
    "SELECT group_col, (jarque_bera).p_value FROM ts_jarque_bera_by('resids', product_id, ds, e)",
    "diagnostics"},

    // ts_residual_diagnostics_by: combined residual adequacy report per group (RESID-04)
    {"ts_residual_diagnostics_by", {"source", "group_col", "date_col", "value_col", nullptr},
     {{"alpha", "0.05"}, {nullptr, nullptr}},
R"(
SELECT group_col, ts_residual_diagnostics(LIST(value_col::DOUBLE ORDER BY date_col), alpha::DOUBLE) AS residual_diagnostics
FROM query_table(source::VARCHAR)
GROUP BY group_col
)",
    "Combined residual adequacy report per group: Ljung-Box + Durbin-Watson + Jarque-Bera. "
    "Returns STRUCT(lb_statistic, lb_p_value, lb_lags, dw_statistic, dw_interpretation, jb_statistic, "
    "jb_p_value, jb_skewness, jb_excess_kurtosis, adequate, dw_p_value). adequate = (lb_p_value > alpha), "
    "alpha default 0.05; dw_p_value is the approximate two-sided Durbin-Watson p-value.",
    "SELECT group_col, (residual_diagnostics).adequate FROM ts_residual_diagnostics_by('resids', product_id, ds, e)",
    "diagnostics"},

    // Sentinel
    {nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr, nullptr, nullptr, nullptr}
};
// clang-format on

// Helper function to create a table macro from the definition
static unique_ptr<CreateMacroInfo> CreateTableMacro(const TsTableMacro &macro_def) {
    // Parse the SQL
    auto parser = MakeParser();
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
        auto expr_list = ParseExpressionList(param.default_value);
        if (!expr_list.empty()) {
            function->default_parameters.insert(AsDuckName(param.name), std::move(expr_list[0]));
        }
    }

    // Create the macro info
    auto info = make_uniq<CreateMacroInfo>(CatalogType::TABLE_MACRO_ENTRY);
    SetCreateInfoName(*info, DEFAULT_SCHEMA, macro_def.name);
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

        // Internal macros (name starts with '_', e.g. _ts_forecast_native, #285/20-05)
        // are not public API and must not gain an anofox_fcst_ alias.
        if (ts_table_macros[i].name[0] == '_') {
            continue;
        }

        // Register the prefixed alias (e.g. anofox_fcst_ts_forecast_by)
        auto alias_info = CreateTableMacro(ts_table_macros[i]);
        SetCreateInfoName(*alias_info, DEFAULT_SCHEMA, "anofox_fcst_" + string(ts_table_macros[i].name));
        alias_info->alias_of = AsDuckName(string(ts_table_macros[i].name));
        loader.RegisterFunction(*alias_info);
    }
}

} // namespace duckdb
