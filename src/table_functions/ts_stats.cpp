#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_fill_gaps_native.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <regex>
#include <map>
#include <mutex>
#include <set>

namespace duckdb {

// Parse frequency string to microseconds and frequency type (e.g., "1d" -> 86400000000, FIXED)
// Returns (microseconds, FrequencyType) - for ts_stats usage
static std::pair<int64_t, FrequencyType> ParseFrequencyForStats(const string &frequency_str) {
    auto parsed = ParseFrequencyWithType(frequency_str);
    // Convert seconds to microseconds for fixed frequencies
    int64_t micros;
    if (parsed.type == FrequencyType::FIXED) {
        micros = parsed.seconds * 1000000LL;
    } else {
        // For calendar frequencies, use approximate values for stats computation
        switch (parsed.type) {
            case FrequencyType::MONTHLY:
                micros = parsed.seconds * 86400LL * 30LL * 1000000LL;
                break;
            case FrequencyType::QUARTERLY:
                micros = parsed.seconds * 86400LL * 90LL * 1000000LL;
                break;
            case FrequencyType::YEARLY:
                micros = parsed.seconds * 86400LL * 365LL * 1000000LL;
                break;
            default:
                micros = parsed.seconds * 1000000LL;
        }
    }
    return std::make_pair(micros, parsed.type);
}

// Parse frequency string to microseconds (e.g., "1d" -> 86400000000) - backward compatible version
static int64_t ParseFrequencyToMicroseconds(const string &frequency_str) {
    return ParseFrequencyForStats(frequency_str).first;
}

// Define the output STRUCT type for ts_stats (34 metrics)
// Note: Using LogicalType(LogicalTypeId::XXX) instead of LogicalType::XXX
// to avoid ODR violations with constexpr static members when linking with duckdb_static
static LogicalType GetTsStatsResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("length", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_nulls", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_nan", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_zeros", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_positive", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_negative", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_unique_values", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("is_constant", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("n_zeros_start", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_zeros_end", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("plateau_size", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("plateau_size_nonzero", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("mean", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("median", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("std_dev", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("variance", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("min", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("max", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("range", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("sum", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("skewness", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("kurtosis", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("tail_index", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("bimodality_coef", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("trimmed_mean", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("coef_variation", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("q1", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("q3", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("iqr", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("autocorr_lag1", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("trend_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("seasonality_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("entropy", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("stability", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("expected_length", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_gaps", LogicalType(LogicalTypeId::UBIGINT)));
    return LogicalType::STRUCT(std::move(children));
}

// Extract values from a LIST vector into a flat array (handles all vector types)
static void ExtractListDoubles(Vector &list_vec, idx_t count, idx_t row_idx,
                               vector<double> &out_values,
                               vector<uint64_t> &out_validity) {
    // Use UnifiedVectorFormat to handle all vector types (flat, constant, dictionary)
    UnifiedVectorFormat list_data;
    list_vec.ToUnifiedFormat(count, list_data);

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto list_idx = list_data.sel->get_index(row_idx);
    auto &list_entry = list_entries[list_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);

    // Also use UnifiedVectorFormat for child vector
    UnifiedVectorFormat child_data;
    child_vec.ToUnifiedFormat(ListVector::GetListSize(list_vec), child_data);
    auto child_values = UnifiedVectorFormat::GetData<double>(child_data);

    out_values.clear();
    out_validity.clear();

    idx_t list_size = list_entry.length;
    idx_t list_offset = list_entry.offset;

    out_values.resize(list_size);
    size_t validity_words = (list_size + 63) / 64;
    out_validity.resize(validity_words, 0);

    for (idx_t i = 0; i < list_size; i++) {
        idx_t child_idx = list_offset + i;
        auto unified_child_idx = child_data.sel->get_index(child_idx);
        if (child_data.validity.RowIsValid(unified_child_idx)) {
            out_values[i] = child_values[unified_child_idx];
            out_validity[i / 64] |= (1ULL << (i % 64));
        } else {
            out_values[i] = 0.0;
        }
    }
}

// Set a STRUCT field value
template <typename T>
static void SetStructField(Vector &result, idx_t field_idx, idx_t row_idx, T value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatVector::GetData<T>(*children[field_idx]);
    data[row_idx] = value;
}

// Extract timestamps from a LIST vector into a flat array (for TIMESTAMP type)
static void ExtractListTimestamps(Vector &list_vec, idx_t count, idx_t row_idx,
                                  vector<int64_t> &out_timestamps) {
    UnifiedVectorFormat list_data;
    list_vec.ToUnifiedFormat(count, list_data);

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto list_idx = list_data.sel->get_index(row_idx);
    auto &list_entry = list_entries[list_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);

    UnifiedVectorFormat child_data;
    child_vec.ToUnifiedFormat(ListVector::GetListSize(list_vec), child_data);
    auto child_values = UnifiedVectorFormat::GetData<timestamp_t>(child_data);

    out_timestamps.clear();

    idx_t list_size = list_entry.length;
    idx_t list_offset = list_entry.offset;

    out_timestamps.resize(list_size);

    for (idx_t i = 0; i < list_size; i++) {
        idx_t child_idx = list_offset + i;
        auto unified_child_idx = child_data.sel->get_index(child_idx);
        if (child_data.validity.RowIsValid(unified_child_idx)) {
            out_timestamps[i] = child_values[unified_child_idx].value;
        } else {
            out_timestamps[i] = 0;
        }
    }
}

// Helper function to populate all 36 stats fields from TsStatsResult
static void PopulateTsStatsResult(Vector &result, idx_t row_idx, const TsStatsResult &stats_result) {
    SetStructField<uint64_t>(result, 0, row_idx, stats_result.length);
    SetStructField<uint64_t>(result, 1, row_idx, stats_result.n_nulls);
    SetStructField<uint64_t>(result, 2, row_idx, stats_result.n_nan);
    SetStructField<uint64_t>(result, 3, row_idx, stats_result.n_zeros);
    SetStructField<uint64_t>(result, 4, row_idx, stats_result.n_positive);
    SetStructField<uint64_t>(result, 5, row_idx, stats_result.n_negative);
    SetStructField<uint64_t>(result, 6, row_idx, stats_result.n_unique_values);
    SetStructField<bool>(result, 7, row_idx, stats_result.is_constant);
    SetStructField<uint64_t>(result, 8, row_idx, stats_result.n_zeros_start);
    SetStructField<uint64_t>(result, 9, row_idx, stats_result.n_zeros_end);
    SetStructField<uint64_t>(result, 10, row_idx, stats_result.plateau_size);
    SetStructField<uint64_t>(result, 11, row_idx, stats_result.plateau_size_nonzero);
    SetStructField<double>(result, 12, row_idx, stats_result.mean);
    SetStructField<double>(result, 13, row_idx, stats_result.median);
    SetStructField<double>(result, 14, row_idx, stats_result.std_dev);
    SetStructField<double>(result, 15, row_idx, stats_result.variance);
    SetStructField<double>(result, 16, row_idx, stats_result.min);
    SetStructField<double>(result, 17, row_idx, stats_result.max);
    SetStructField<double>(result, 18, row_idx, stats_result.range);
    SetStructField<double>(result, 19, row_idx, stats_result.sum);
    SetStructField<double>(result, 20, row_idx, stats_result.skewness);
    SetStructField<double>(result, 21, row_idx, stats_result.kurtosis);
    SetStructField<double>(result, 22, row_idx, stats_result.tail_index);
    SetStructField<double>(result, 23, row_idx, stats_result.bimodality_coef);
    SetStructField<double>(result, 24, row_idx, stats_result.trimmed_mean);
    SetStructField<double>(result, 25, row_idx, stats_result.coef_variation);
    SetStructField<double>(result, 26, row_idx, stats_result.q1);
    SetStructField<double>(result, 27, row_idx, stats_result.q3);
    SetStructField<double>(result, 28, row_idx, stats_result.iqr);
    SetStructField<double>(result, 29, row_idx, stats_result.autocorr_lag1);
    SetStructField<double>(result, 30, row_idx, stats_result.trend_strength);
    SetStructField<double>(result, 31, row_idx, stats_result.seasonality_strength);
    SetStructField<double>(result, 32, row_idx, stats_result.entropy);
    SetStructField<double>(result, 33, row_idx, stats_result.stability);

    // Set date-based metrics
    auto &children = StructVector::GetEntries(result);
    if (stats_result.has_date_metrics) {
        SetStructField<uint64_t>(result, 34, row_idx, stats_result.expected_length);
        SetStructField<uint64_t>(result, 35, row_idx, stats_result.n_gaps);
    } else {
        FlatVector::SetNull(*children[34], row_idx, true);
        FlatVector::SetNull(*children[35], row_idx, true);
    }
}

// Main scalar function for ts_stats (values only, no dates)
static void TsStatsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat list_format;
    list_vec.ToUnifiedFormat(count, list_format);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto list_idx = list_format.sel->get_index(row_idx);
        if (!list_format.validity.RowIsValid(list_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListDoubles(list_vec, count, row_idx, values, validity);

        TsStatsResult stats_result;
        AnofoxError error;

        bool success = anofox_ts_stats(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            &stats_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        PopulateTsStatsResult(result, row_idx, stats_result);
        anofox_free_ts_stats_result(&stats_result);
    }
}

// Scalar function for ts_stats with dates and frequency
// Takes: LIST(DOUBLE) values, LIST(TIMESTAMP) dates, VARCHAR frequency
static void TsStatsWithDatesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &dates_vec = args.data[1];
    auto &freq_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat values_format, dates_format, freq_format;
    values_vec.ToUnifiedFormat(count, values_format);
    dates_vec.ToUnifiedFormat(count, dates_format);
    freq_vec.ToUnifiedFormat(count, freq_format);

    auto freq_data = UnifiedVectorFormat::GetData<string_t>(freq_format);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto values_idx = values_format.sel->get_index(row_idx);
        auto dates_idx = dates_format.sel->get_index(row_idx);
        auto freq_idx = freq_format.sel->get_index(row_idx);

        // Check if any input is NULL
        if (!values_format.validity.RowIsValid(values_idx) ||
            !dates_format.validity.RowIsValid(dates_idx) ||
            !freq_format.validity.RowIsValid(freq_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Extract values
        vector<double> values;
        vector<uint64_t> validity;
        ExtractListDoubles(values_vec, count, row_idx, values, validity);

        // Extract timestamps
        vector<int64_t> timestamps;
        ExtractListTimestamps(dates_vec, count, row_idx, timestamps);

        // Parse frequency
        string freq_str = freq_data[freq_idx].GetString();
        int64_t frequency_micros = ParseFrequencyToMicroseconds(freq_str);

        // Call Rust FFI function with dates
        TsStatsResult stats_result;
        AnofoxError error;

        bool success = anofox_ts_stats_with_dates(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            timestamps.data(),
            values.size(),
            frequency_micros,
            &stats_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        PopulateTsStatsResult(result, row_idx, stats_result);
        anofox_free_ts_stats_result(&stats_result);
    }
}

// ============================================================================
// ts_stats_by - Native Table Function
// ============================================================================
// This replaces the SQL macro to properly preserve input column names and
// correctly handle calendar frequencies (monthly, quarterly, yearly).

struct TsStatsByBindData : public TableFunctionData {
    int64_t frequency_micros = 86400LL * 1000000LL;  // Default: 1 day
    FrequencyType frequency_type = FIXED;
    string group_col_name;  // Preserved from input
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Global State - enables parallel execution
//
// IMPORTANT: This custom GlobalState is required for proper parallel execution.
// Using the base GlobalTableFunctionState directly causes batch index collisions
// with large datasets (300k+ groups) during BatchedDataCollection::Merge.
// ============================================================================

struct TsStatsByGlobalState : public GlobalTableFunctionState {
    // Allow parallel execution - each thread processes its partition of groups
    idx_t MaxThreads() const override {
        return 999999;  // Unlimited - let DuckDB decide based on hardware
    }

    // Global group tracking to prevent duplicate processing
    std::mutex processed_groups_mutex;
    std::set<string> processed_groups;

    bool ClaimGroup(const string &group_key) {
        std::lock_guard<std::mutex> lock(processed_groups_mutex);
        auto result = processed_groups.insert(group_key);
        return result.second;
    }
};

struct TsStatsByLocalState : public LocalTableFunctionState {
    // Buffer incoming data per group
    struct GroupData {
        Value group_value;
        vector<int64_t> timestamps;  // microseconds
        vector<double> values;
        vector<bool> validity;
    };

    std::map<string, GroupData> groups;
    vector<string> group_order;

    // Output results
    struct StatsOutputRow {
        string group_key;
        Value group_value;
        TsStatsResult stats;
    };
    vector<StatsOutputRow> results;

    // Processing state
    bool processed = false;
    idx_t output_offset = 0;
};

static unique_ptr<FunctionData> TsStatsByBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsStatsByBindData>();

    // Parse frequency from second argument (index 1, since index 0 is TABLE placeholder)
    if (input.inputs.size() >= 2) {
        string freq_str = input.inputs[1].GetValue<string>();
        auto [micros, ftype] = ParseFrequencyForStats(freq_str);
        bind_data->frequency_micros = micros;
        bind_data->frequency_type = ftype;
    }

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "ts_stats_by requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Preserve the group column name from input
    bind_data->group_col_name = input.input_table_names[0];
    bind_data->group_logical_type = input.input_table_types[0];

    // Detect date column type from input (column 1)
    switch (input.input_table_types[1].id()) {
        case LogicalTypeId::DATE:
            bind_data->date_col_type = DateColumnType::DATE;
            break;
        case LogicalTypeId::TIMESTAMP:
        case LogicalTypeId::TIMESTAMP_TZ:
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
            break;
        default:
            throw InvalidInputException(
                "Date column must be DATE or TIMESTAMP, got: %s",
                input.input_table_types[1].ToString().c_str());
    }

    // Output schema: group_col (with preserved name), then all 36 stats columns
    names.push_back(bind_data->group_col_name);  // Use the preserved input column name!
    return_types.push_back(bind_data->group_logical_type);

    // Add all 36 stats columns
    names.push_back("length");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_nulls");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_nan");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_zeros");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_positive");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_negative");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_unique_values");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("is_constant");
    return_types.push_back(LogicalType(LogicalTypeId::BOOLEAN));
    names.push_back("n_zeros_start");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_zeros_end");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("plateau_size");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("plateau_size_nonzero");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("mean");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("median");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("std_dev");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("variance");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("min");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("max");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("range");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("sum");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("skewness");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("kurtosis");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("tail_index");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("bimodality_coef");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("trimmed_mean");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("coef_variation");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("q1");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("q3");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("iqr");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("autocorr_lag1");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("trend_strength");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("seasonality_strength");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("entropy");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("stability");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    names.push_back("expected_length");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));
    names.push_back("n_gaps");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsStatsByInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsStatsByGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsStatsByInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsStatsByLocalState>();
}

static OperatorResultType TsStatsByInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsStatsByBindData>();
    auto &local_state = data_p.local_state->Cast<TsStatsByLocalState>();

    // Buffer all incoming data - we need complete groups before processing
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsStatsByLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        // Convert date to microseconds
        int64_t date_micros;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
            default:
                date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
        }

        grp.timestamps.push_back(date_micros);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
        grp.validity.push_back(!value_val.IsNull());
    }

    // Don't output anything during input phase - wait for finalize
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType TsStatsByFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsStatsByBindData>();
    auto &local_state = data_p.local_state->Cast<TsStatsByLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            auto &grp = local_state.groups[group_key];

            if (grp.timestamps.empty()) continue;

            // Build validity bitmask for Rust
            size_t validity_words = (grp.timestamps.size() + 63) / 64;
            vector<uint64_t> validity(validity_words, 0);
            for (size_t i = 0; i < grp.validity.size(); i++) {
                if (grp.validity[i]) {
                    validity[i / 64] |= (1ULL << (i % 64));
                }
            }

            // Call Rust FFI function with dates and frequency type
            TsStatsResult stats_result = {};
            AnofoxError error = {};

            bool success = anofox_ts_stats_with_dates_and_type(
                grp.values.data(),
                validity.empty() ? nullptr : validity.data(),
                grp.timestamps.data(),
                grp.values.size(),
                bind_data.frequency_micros,
                bind_data.frequency_type,
                &stats_result,
                &error
            );

            if (!success) {
                throw InvalidInputException("ts_stats_by failed: %s",
                    error.message ? error.message : "Unknown error");
            }

            // Store results
            TsStatsByLocalState::StatsOutputRow row;
            row.group_key = group_key;
            row.group_value = grp.group_value;
            row.stats = stats_result;
            local_state.results.push_back(std::move(row));
        }
        local_state.processed = true;
    }

    // Output results
    if (local_state.results.empty() || local_state.output_offset >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    // Initialize all output vectors as FLAT_VECTOR
    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    while (output_count < STANDARD_VECTOR_SIZE && local_state.output_offset < local_state.results.size()) {
        auto &row = local_state.results[local_state.output_offset];
        auto &stats = row.stats;
        idx_t out_idx = output_count;

        // Column 0: Group column (with preserved name)
        output.data[0].SetValue(out_idx, row.group_value);

        // Columns 1-36: Stats values
        FlatVector::GetData<uint64_t>(output.data[1])[out_idx] = stats.length;
        FlatVector::GetData<uint64_t>(output.data[2])[out_idx] = stats.n_nulls;
        FlatVector::GetData<uint64_t>(output.data[3])[out_idx] = stats.n_nan;
        FlatVector::GetData<uint64_t>(output.data[4])[out_idx] = stats.n_zeros;
        FlatVector::GetData<uint64_t>(output.data[5])[out_idx] = stats.n_positive;
        FlatVector::GetData<uint64_t>(output.data[6])[out_idx] = stats.n_negative;
        FlatVector::GetData<uint64_t>(output.data[7])[out_idx] = stats.n_unique_values;
        FlatVector::GetData<bool>(output.data[8])[out_idx] = stats.is_constant;
        FlatVector::GetData<uint64_t>(output.data[9])[out_idx] = stats.n_zeros_start;
        FlatVector::GetData<uint64_t>(output.data[10])[out_idx] = stats.n_zeros_end;
        FlatVector::GetData<uint64_t>(output.data[11])[out_idx] = stats.plateau_size;
        FlatVector::GetData<uint64_t>(output.data[12])[out_idx] = stats.plateau_size_nonzero;
        FlatVector::GetData<double>(output.data[13])[out_idx] = stats.mean;
        FlatVector::GetData<double>(output.data[14])[out_idx] = stats.median;
        FlatVector::GetData<double>(output.data[15])[out_idx] = stats.std_dev;
        FlatVector::GetData<double>(output.data[16])[out_idx] = stats.variance;
        FlatVector::GetData<double>(output.data[17])[out_idx] = stats.min;
        FlatVector::GetData<double>(output.data[18])[out_idx] = stats.max;
        FlatVector::GetData<double>(output.data[19])[out_idx] = stats.range;
        FlatVector::GetData<double>(output.data[20])[out_idx] = stats.sum;
        FlatVector::GetData<double>(output.data[21])[out_idx] = stats.skewness;
        FlatVector::GetData<double>(output.data[22])[out_idx] = stats.kurtosis;
        FlatVector::GetData<double>(output.data[23])[out_idx] = stats.tail_index;
        FlatVector::GetData<double>(output.data[24])[out_idx] = stats.bimodality_coef;
        FlatVector::GetData<double>(output.data[25])[out_idx] = stats.trimmed_mean;
        FlatVector::GetData<double>(output.data[26])[out_idx] = stats.coef_variation;
        FlatVector::GetData<double>(output.data[27])[out_idx] = stats.q1;
        FlatVector::GetData<double>(output.data[28])[out_idx] = stats.q3;
        FlatVector::GetData<double>(output.data[29])[out_idx] = stats.iqr;
        FlatVector::GetData<double>(output.data[30])[out_idx] = stats.autocorr_lag1;
        FlatVector::GetData<double>(output.data[31])[out_idx] = stats.trend_strength;
        FlatVector::GetData<double>(output.data[32])[out_idx] = stats.seasonality_strength;
        FlatVector::GetData<double>(output.data[33])[out_idx] = stats.entropy;
        FlatVector::GetData<double>(output.data[34])[out_idx] = stats.stability;

        // expected_length and n_gaps - handle NULL if no date metrics
        if (stats.has_date_metrics) {
            FlatVector::GetData<uint64_t>(output.data[35])[out_idx] = stats.expected_length;
            FlatVector::GetData<uint64_t>(output.data[36])[out_idx] = stats.n_gaps;
        } else {
            FlatVector::SetNull(output.data[35], out_idx, true);
            FlatVector::SetNull(output.data[36], out_idx, true);
        }

        output_count++;
        local_state.output_offset++;
    }

    output.SetCardinality(output_count);

    if (local_state.output_offset >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

void RegisterTsStatsByFunction(ExtensionLoader &loader) {
    // Internal native table function: _ts_stats_by_native(TABLE, frequency)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Called by ts_stats_by SQL macro to preserve column names and handle calendar frequencies
    // This is an internal function - users should call ts_stats_by() instead
    TableFunction func("_ts_stats_by_native",
        {LogicalType::TABLE, LogicalType(LogicalTypeId::VARCHAR)},
        nullptr,  // No execute function - use in_out_function
        TsStatsByBind,
        TsStatsByInitGlobal,
        TsStatsByInitLocal);

    func.in_out_function = TsStatsByInOut;
    func.in_out_function_final = TsStatsByFinalize;

    loader.RegisterFunction(func);
}

// ============================================================================
// Scalar Functions Registration
// ============================================================================

void RegisterTsStatsFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_stats table macro (values only)
    // Named with underscore prefix to match C++ API (ts_stats is table macro only)
    ScalarFunctionSet ts_stats_set("_ts_stats");

    ScalarFunction ts_stats_func(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetTsStatsResultType(),
        TsStatsFunction
    );
    ts_stats_func.stability = FunctionStability::VOLATILE;
    ts_stats_set.AddFunction(ts_stats_func);

    CreateScalarFunctionInfo info(ts_stats_set);
    info.internal = true;
    loader.RegisterFunction(info);

    // Internal scalar function with dates and frequency support
    // _ts_stats_with_dates(values[], dates[], frequency)
    ScalarFunctionSet ts_stats_with_dates_set("_ts_stats_with_dates");

    ScalarFunction ts_stats_with_dates_func(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP)),
         LogicalType(LogicalTypeId::VARCHAR)},
        GetTsStatsResultType(),
        TsStatsWithDatesFunction
    );
    ts_stats_with_dates_func.stability = FunctionStability::VOLATILE;
    ts_stats_with_dates_set.AddFunction(ts_stats_with_dates_func);

    CreateScalarFunctionInfo with_dates_info(ts_stats_with_dates_set);
    with_dates_info.internal = true;
    loader.RegisterFunction(with_dates_info);
}

} // namespace duckdb
