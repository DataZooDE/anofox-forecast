#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

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
    return LogicalType::STRUCT(std::move(children));
}

// Extract values from a LIST vector into a flat array (handles all vector types)
static void ExtractListValues(Vector &list_vec, idx_t count, idx_t row_idx,
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

// Main scalar function for ts_stats
static void TsStatsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat list_format;
    list_vec.ToUnifiedFormat(count, list_format);

    // Process each row
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto list_idx = list_format.sel->get_index(row_idx);
        // Check if input is NULL
        if (!list_format.validity.RowIsValid(list_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Extract values from the list
        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, count, row_idx, values, validity);

        // Call Rust FFI function
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

        // Set result fields (34 metrics)
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

        // Free Rust-allocated memory (no-op for TsStatsResult)
        anofox_free_ts_stats_result(&stats_result);
    }
}

void RegisterTsStatsFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_stats table macro
    // Named with underscore prefix to match C++ API (ts_stats is table macro only)
    ScalarFunctionSet ts_stats_set("_ts_stats");

    // Mark as VOLATILE to prevent constant folding (statistics computation shouldn't be folded)
    ScalarFunction ts_stats_func(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetTsStatsResultType(),
        TsStatsFunction
    );
    ts_stats_func.stability = FunctionStability::VOLATILE;
    ts_stats_set.AddFunction(ts_stats_func);

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_stats_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

} // namespace duckdb
