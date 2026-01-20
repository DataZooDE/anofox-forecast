#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <regex>

namespace duckdb {

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
