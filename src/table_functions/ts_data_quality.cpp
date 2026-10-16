#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_fill_gaps_native.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include "ts_list_marshal.hpp"

namespace duckdb {

// Frequency parsing for ts_data_quality now shared via ParseFrequencyToMicrosAndType
// (ts_fill_gaps_native.hpp/.cpp; CR-01 fix, WR-02) -- the previous
// ParseFrequencyForDataQuality, a verbatim copy-paste of ts_stats.cpp's
// ParseFrequencyForStats, is removed in favor of the single shared helper.

static LogicalType GetDataQualityResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("structural_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("temporal_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("magnitude_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("behavioral_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("overall_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("n_gaps", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_gap_runs", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_missing", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("is_constant", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("is_short", LogicalType(LogicalTypeId::BOOLEAN)));
    return LogicalType::STRUCT(std::move(children));
}

template <typename T>
static void SetStructField(Vector &result, idx_t field_idx, idx_t row_idx, T value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatDataMut<T>(StructEntry(children[field_idx]));
    data[row_idx] = value;
}

// Extract timestamps from a LIST vector into a flat array (mirrors
// ts_stats.cpp's ExtractListTimestamps).
//
// WR-01 fix: a NULL element *inside* an otherwise non-NULL dates list previously
// silently became epoch 0 (1970-01-01), which -- once sorted into the series --
// looks like a multi-decade gap to gap_counts/resolve_gap_step, corrupting
// n_gaps/n_gap_runs/expected_length/temporal_score for the whole row without any
// error. Throws instead, naming the offending row and position, matching the
// existing "dates and values length mismatch" error style.
static void ExtractListTimestamps(Vector &list_vec, idx_t count, idx_t row_idx,
                                  vector<int64_t> &out_timestamps) {
    UnifiedVectorFormat list_data;
    ToUnified(list_vec, count, list_data);

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto list_idx = list_data.sel->get_index(row_idx);
    auto &list_entry = list_entries[list_idx];

    auto &child_vec = ListChildMut(list_vec);

    UnifiedVectorFormat child_data;
    ToUnified(child_vec, ListVector::GetListSize(list_vec), child_data);
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
            throw InvalidInputException(
                "ts_data_quality: dates list for row %llu contains a NULL element at "
                "position %llu; every element of a declared dates list must be "
                "non-NULL (pass a NULL dates list itself for \"no date information\")",
                (unsigned long long)row_idx, (unsigned long long)i);
        }
    }
}

// _ts_data_quality(values DOUBLE[], dates TIMESTAMP[], frequency VARCHAR, n_short INTEGER)
// D-05/WRAP-02: wires dates, frequency and n_short through to compute_data_quality on
// the shared gap counter (plan 13-01).
static void TsDataQualityFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &values_vec = args.data[0];
    auto &dates_vec = args.data[1];
    auto &freq_vec = args.data[2];
    auto &n_short_vec = args.data[3];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat dates_format, freq_format, n_short_format;
    ToUnified(dates_vec, count, dates_format);
    ToUnified(freq_vec, count, freq_format);
    ToUnified(n_short_vec, count, n_short_format);

    auto freq_data = UnifiedVectorFormat::GetData<string_t>(freq_format);
    auto n_short_data = UnifiedVectorFormat::GetData<int32_t>(n_short_format);

    ListDoubleReader values_reader(values_vec, count);
    vector<double> values;
    vector<uint64_t> validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto dates_idx = dates_format.sel->get_index(row_idx);
        auto n_short_idx = n_short_format.sel->get_index(row_idx);

        if (values_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        values_reader.Read(row_idx, values, validity);

        // dates: NULL list -> no date information (FFI dates pointer stays null).
        bool has_dates = dates_format.validity.RowIsValid(dates_idx);
        vector<int64_t> timestamps;
        if (has_dates) {
            ExtractListTimestamps(dates_vec, count, row_idx, timestamps);
            if (timestamps.size() != values.size()) {
                throw InvalidInputException(
                    "ts_data_quality: dates and values must have the same length, got %zu dates and %zu values",
                    timestamps.size(), values.size());
            }
        }

        // frequency: NULL or '' -> auto-detect (micros 0, FIXED, period_count 1).
        int64_t frequency_micros = 0;
        FrequencyType frequency_type = FrequencyType::FIXED;
        int64_t frequency_period_count = 1;  // CR-01 fix: calendar multiplier, e.g. 2 for '2mo'
        auto freq_idx = freq_format.sel->get_index(row_idx);
        if (freq_format.validity.RowIsValid(freq_idx)) {
            string freq_str = freq_data[freq_idx].GetString();
            if (!freq_str.empty()) {
                auto parsed = ParseFrequencyToMicrosAndType(freq_str);
                frequency_micros = parsed.micros;
                frequency_type = parsed.type;
                frequency_period_count = parsed.period_count;
            }
        }

        // n_short: NULL or < 1 -> error naming ts_data_quality.
        if (!n_short_format.validity.RowIsValid(n_short_idx)) {
            throw InvalidInputException("ts_data_quality: n_short must be >= 1, got NULL");
        }
        int64_t n_short = n_short_data[n_short_idx];
        if (n_short < 1) {
            throw InvalidInputException("ts_data_quality: n_short must be >= 1, got %lld",
                                        (long long)n_short);
        }

        DataQualityResult dq_result;
        AnofoxError error;

        bool success = anofox_ts_data_quality(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            has_dates ? timestamps.data() : nullptr,
            values.size(),
            frequency_micros,
            frequency_type,
            static_cast<size_t>(n_short),
            static_cast<size_t>(frequency_period_count),
            &dq_result,
            &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("ts_data_quality: %s",
                    error.message ? error.message : "invalid input");
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        SetStructField<double>(result, 0, row_idx, dq_result.structural_score);
        SetStructField<double>(result, 1, row_idx, dq_result.temporal_score);
        SetStructField<double>(result, 2, row_idx, dq_result.magnitude_score);
        SetStructField<double>(result, 3, row_idx, dq_result.behavioral_score);
        SetStructField<double>(result, 4, row_idx, dq_result.overall_score);
        SetStructField<uint64_t>(result, 5, row_idx, dq_result.n_gaps);
        SetStructField<uint64_t>(result, 6, row_idx, dq_result.n_gap_runs);
        SetStructField<uint64_t>(result, 7, row_idx, dq_result.n_missing);
        SetStructField<bool>(result, 8, row_idx, dq_result.is_constant);
        SetStructField<bool>(result, 9, row_idx, dq_result.is_short);
    }
}

void RegisterTsDataQualityFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_data_quality table macro
    // Named with underscore prefix to match C++ API (ts_data_quality is table macro only)
    ScalarFunctionSet ts_dq_set("_ts_data_quality");
    ScalarFunction ts_dq_func(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP)),
         LogicalType(LogicalTypeId::VARCHAR),
         LogicalType(LogicalTypeId::INTEGER)},
        GetDataQualityResultType(),
        TsDataQualityFunction
    );
    MarkFallible(ts_dq_func);
    SetStabilityOf(ts_dq_func, FunctionStability::VOLATILE);
    // frequency (and dates) may legitimately be NULL (auto-detect / no date info);
    // disable DuckDB's default NULL-in-NULL-out propagation so the function body's
    // own NULL handling (frequency -> auto, dates -> none) actually runs.
    SetNullHandlingOf(ts_dq_func, FunctionNullHandling::SPECIAL_HANDLING);
    ts_dq_set.AddFunction(ts_dq_func);

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_dq_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

// Placeholder for summary function
void RegisterTsDataQualitySummaryFunction(ExtensionLoader &loader) {
    // TODO: Implement aggregate version
}

} // namespace duckdb
