#include "ts_stats_native.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <regex>
#include <cmath>
#include <map>

namespace duckdb {

// ============================================================================
// Helper Functions
// ============================================================================

// Parse frequency string to microseconds (e.g., "1d" -> 86400000000)
static int64_t ParseFrequencyToMicroseconds(const string &frequency_str) {
    string upper = StringUtil::Upper(frequency_str);
    StringUtil::Trim(upper);

    // Try to parse Polars-style frequency (e.g., "1d", "1h", "30m")
    std::regex polars_regex("^([0-9]+)(d|h|m|min|w|mo|q|y)$", std::regex::icase);
    std::smatch match;

    if (std::regex_match(upper, match, polars_regex)) {
        int64_t count = std::stoll(match[1].str());
        string unit = StringUtil::Lower(match[2].str());

        if (unit == "d") return count * 86400LL * 1000000LL;
        if (unit == "h") return count * 3600LL * 1000000LL;
        if (unit == "m" || unit == "min") return count * 60LL * 1000000LL;
        if (unit == "w") return count * 86400LL * 7LL * 1000000LL;
        if (unit == "mo") return count * 86400LL * 30LL * 1000000LL;
        if (unit == "q") return count * 86400LL * 90LL * 1000000LL;
        if (unit == "y") return count * 86400LL * 365LL * 1000000LL;
    }

    // Try to parse DuckDB INTERVAL style (e.g., "1 day", "1 hour")
    std::regex interval_regex("^([0-9]+)\\s*(day|days|hour|hours|minute|minutes|week|weeks|month|months|year|years)$", std::regex::icase);

    if (std::regex_match(upper, match, interval_regex)) {
        int64_t count = std::stoll(match[1].str());
        string unit = StringUtil::Lower(match[2].str());

        if (unit == "day" || unit == "days") return count * 86400LL * 1000000LL;
        if (unit == "hour" || unit == "hours") return count * 3600LL * 1000000LL;
        if (unit == "minute" || unit == "minutes") return count * 60LL * 1000000LL;
        if (unit == "week" || unit == "weeks") return count * 86400LL * 7LL * 1000000LL;
        if (unit == "month" || unit == "months") return count * 86400LL * 30LL * 1000000LL;
        if (unit == "year" || unit == "years") return count * 86400LL * 365LL * 1000000LL;
    }

    // Default to 1 day
    return 86400LL * 1000000LL;
}

// Convert various date types to microseconds
static int64_t DateToMicroseconds(date_t date) {
    return static_cast<int64_t>(date.days) * 24LL * 60LL * 60LL * 1000000LL;
}

static int64_t TimestampToMicroseconds(timestamp_t ts) {
    return ts.value;
}

// Generate group key for map lookup
static string GetGroupKey(const Value &group_value) {
    if (group_value.IsNull()) {
        return "__NULL__";
    }
    return group_value.ToString();
}

// ============================================================================
// Bind Data
// ============================================================================

struct TsStatsNativeBindData : public TableFunctionData {
    int64_t frequency_micros = 86400LL * 1000000LL;  // Default: 1 day
    string group_col_name;
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Local State - buffers data per thread
// ============================================================================

struct TsStatsNativeLocalState : public LocalTableFunctionState {
    // Buffer for incoming data per group
    struct GroupData {
        Value group_value;
        vector<int64_t> dates;
        vector<double> values;
        vector<bool> validity;
    };

    // Map group key -> accumulated data
    std::map<string, GroupData> groups;
    vector<string> group_order;  // Track insertion order

    // Stats results ready to output
    struct StatsGroup {
        Value group_value;
        TsStatsResult stats;
    };
    vector<StatsGroup> stats_results;
    idx_t current_group = 0;
    bool processed = false;
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsStatsNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsStatsNativeBindData>();

    // Parse frequency from second argument (index 1, since index 0 is TABLE placeholder)
    if (input.inputs.size() >= 2) {
        string freq_str = input.inputs[1].GetValue<string>();
        bind_data->frequency_micros = ParseFrequencyToMicroseconds(freq_str);
    }

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "_ts_stats_native requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Store group column info
    bind_data->group_col_name = input.input_table_names[0];
    bind_data->group_logical_type = input.input_table_types[0];

    // Output schema: preserve original group column name + 35 stats columns
    names.push_back(bind_data->group_col_name);
    return_types.push_back(bind_data->group_logical_type);

    // Add all 35 stats columns
    names.push_back("length"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_nulls"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_nan"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_zeros"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_positive"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_negative"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_unique_values"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("is_constant"); return_types.push_back(LogicalType(LogicalTypeId::BOOLEAN));
    names.push_back("n_zeros_start"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_zeros_end"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("plateau_size"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("plateau_size_nonzero"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("mean"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("median"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("std_dev"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("variance"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("min"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("max"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("range"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("sum"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("skewness"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("kurtosis"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("tail_index"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("bimodality_coef"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("trimmed_mean"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("coef_variation"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("q1"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("q3"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("iqr"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("autocorr_lag1"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("trend_strength"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("seasonality_strength"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("entropy"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("stability"); return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("expected_length"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_gaps"); return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsStatsNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<GlobalTableFunctionState>();
}

static unique_ptr<LocalTableFunctionState> TsStatsNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsStatsNativeLocalState>();
}

// ============================================================================
// In-Out Function - receives streaming input
// ============================================================================

static OperatorResultType TsStatsNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &local_state = data_p.local_state->Cast<TsStatsNativeLocalState>();

    // Buffer all incoming data - we need complete groups before processing
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsStatsNativeLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        // Convert date to microseconds for Rust
        int64_t date_micros;
        auto date_type = date_val.type().id();
        switch (date_type) {
            case LogicalTypeId::DATE:
                date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case LogicalTypeId::TIMESTAMP:
            case LogicalTypeId::TIMESTAMP_TZ:
                date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
            case LogicalTypeId::INTEGER:
                // Treat integer as days, convert to microseconds
                date_micros = static_cast<int64_t>(date_val.GetValue<int32_t>()) * 86400LL * 1000000LL;
                break;
            case LogicalTypeId::BIGINT:
                // Treat bigint as microseconds
                date_micros = date_val.GetValue<int64_t>();
                break;
            default:
                // Fallback: try to cast to timestamp
                date_micros = date_val.GetValue<timestamp_t>().value;
                break;
        }

        grp.dates.push_back(date_micros);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
        grp.validity.push_back(!value_val.IsNull());
    }

    // Don't output anything during input phase - wait for finalize
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - process accumulated data and output results
// ============================================================================

static OperatorFinalizeResultType TsStatsNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsStatsNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsStatsNativeLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            auto &grp = local_state.groups[group_key];

            if (grp.dates.empty()) continue;

            // Sort by date to ensure correct order
            vector<size_t> indices(grp.dates.size());
            for (size_t i = 0; i < indices.size(); i++) {
                indices[i] = i;
            }
            std::sort(indices.begin(), indices.end(), [&grp](size_t a, size_t b) {
                return grp.dates[a] < grp.dates[b];
            });

            // Reorder arrays by date
            vector<int64_t> sorted_dates(grp.dates.size());
            vector<double> sorted_values(grp.values.size());
            vector<bool> sorted_validity(grp.validity.size());
            for (size_t i = 0; i < indices.size(); i++) {
                sorted_dates[i] = grp.dates[indices[i]];
                sorted_values[i] = grp.values[indices[i]];
                sorted_validity[i] = grp.validity[indices[i]];
            }

            // Build validity bitmask for Rust
            size_t validity_words = (sorted_dates.size() + 63) / 64;
            vector<uint64_t> validity(validity_words, 0);
            for (size_t i = 0; i < sorted_validity.size(); i++) {
                if (sorted_validity[i]) {
                    validity[i / 64] |= (1ULL << (i % 64));
                }
            }

            // Call Rust FFI
            TsStatsResult stats_result = {};
            AnofoxError error = {};

            bool success = anofox_ts_stats_with_dates(
                sorted_values.data(),
                validity.empty() ? nullptr : validity.data(),
                sorted_dates.data(),
                sorted_values.size(),
                bind_data.frequency_micros,
                &stats_result,
                &error
            );

            if (!success) {
                throw InvalidInputException("_ts_stats_native failed: %s",
                    error.message ? error.message : "Unknown error");
            }

            // Store results
            TsStatsNativeLocalState::StatsGroup stats_grp;
            stats_grp.group_value = grp.group_value;
            stats_grp.stats = stats_result;

            local_state.stats_results.push_back(std::move(stats_grp));
        }
        local_state.processed = true;
    }

    // Output results
    if (local_state.stats_results.empty() ||
        local_state.current_group >= local_state.stats_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    while (output_count < STANDARD_VECTOR_SIZE &&
           local_state.current_group < local_state.stats_results.size()) {

        auto &grp = local_state.stats_results[local_state.current_group];
        idx_t out_idx = output_count;

        // Column 0: Group column (preserve original type and name)
        output.data[0].SetValue(out_idx, grp.group_value);

        // Columns 1-36: Stats columns
        auto &stats = grp.stats;
        output.data[1].SetValue(out_idx, Value::UBIGINT(stats.length));
        output.data[2].SetValue(out_idx, Value::UBIGINT(stats.n_nulls));
        output.data[3].SetValue(out_idx, Value::UBIGINT(stats.n_nan));
        output.data[4].SetValue(out_idx, Value::UBIGINT(stats.n_zeros));
        output.data[5].SetValue(out_idx, Value::UBIGINT(stats.n_positive));
        output.data[6].SetValue(out_idx, Value::UBIGINT(stats.n_negative));
        output.data[7].SetValue(out_idx, Value::UBIGINT(stats.n_unique_values));
        output.data[8].SetValue(out_idx, Value::BOOLEAN(stats.is_constant));
        output.data[9].SetValue(out_idx, Value::UBIGINT(stats.n_zeros_start));
        output.data[10].SetValue(out_idx, Value::UBIGINT(stats.n_zeros_end));
        output.data[11].SetValue(out_idx, Value::UBIGINT(stats.plateau_size));
        output.data[12].SetValue(out_idx, Value::UBIGINT(stats.plateau_size_nonzero));
        output.data[13].SetValue(out_idx, Value::DOUBLE(stats.mean));
        output.data[14].SetValue(out_idx, Value::DOUBLE(stats.median));
        output.data[15].SetValue(out_idx, Value::DOUBLE(stats.std_dev));
        output.data[16].SetValue(out_idx, Value::DOUBLE(stats.variance));
        output.data[17].SetValue(out_idx, Value::DOUBLE(stats.min));
        output.data[18].SetValue(out_idx, Value::DOUBLE(stats.max));
        output.data[19].SetValue(out_idx, Value::DOUBLE(stats.range));
        output.data[20].SetValue(out_idx, Value::DOUBLE(stats.sum));
        output.data[21].SetValue(out_idx, Value::DOUBLE(stats.skewness));
        output.data[22].SetValue(out_idx, Value::DOUBLE(stats.kurtosis));
        output.data[23].SetValue(out_idx, Value::DOUBLE(stats.tail_index));
        output.data[24].SetValue(out_idx, Value::DOUBLE(stats.bimodality_coef));
        output.data[25].SetValue(out_idx, Value::DOUBLE(stats.trimmed_mean));
        output.data[26].SetValue(out_idx, Value::DOUBLE(stats.coef_variation));
        output.data[27].SetValue(out_idx, Value::DOUBLE(stats.q1));
        output.data[28].SetValue(out_idx, Value::DOUBLE(stats.q3));
        output.data[29].SetValue(out_idx, Value::DOUBLE(stats.iqr));
        output.data[30].SetValue(out_idx, Value::DOUBLE(stats.autocorr_lag1));
        output.data[31].SetValue(out_idx, Value::DOUBLE(stats.trend_strength));
        output.data[32].SetValue(out_idx, Value::DOUBLE(stats.seasonality_strength));
        output.data[33].SetValue(out_idx, Value::DOUBLE(stats.entropy));
        output.data[34].SetValue(out_idx, Value::DOUBLE(stats.stability));

        // Date-based metrics (may be NULL if not computed)
        if (stats.has_date_metrics) {
            output.data[35].SetValue(out_idx, Value::UBIGINT(stats.expected_length));
            output.data[36].SetValue(out_idx, Value::UBIGINT(stats.n_gaps));
        } else {
            output.data[35].SetValue(out_idx, Value());
            output.data[36].SetValue(out_idx, Value());
        }

        // Free the stats result
        anofox_free_ts_stats_result(const_cast<TsStatsResult*>(&grp.stats));

        output_count++;
        local_state.current_group++;
    }

    output.SetCardinality(output_count);

    if (local_state.current_group >= local_state.stats_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsStatsNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, frequency)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_stats_by macro
    TableFunction func("_ts_stats_native",
        {LogicalType::TABLE, LogicalType(LogicalTypeId::VARCHAR)},
        nullptr,  // No execute function - use in_out_function
        TsStatsNativeBind,
        TsStatsNativeInitGlobal,
        TsStatsNativeInitLocal);

    func.in_out_function = TsStatsNativeInOut;
    func.in_out_function_final = TsStatsNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
