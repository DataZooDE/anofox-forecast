#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

// Define the output STRUCT type for ts_stats (34 metrics)
static LogicalType GetTsStatsResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("length", LogicalType::UBIGINT));
    children.push_back(make_pair("n_nulls", LogicalType::UBIGINT));
    children.push_back(make_pair("n_nan", LogicalType::UBIGINT));
    children.push_back(make_pair("n_zeros", LogicalType::UBIGINT));
    children.push_back(make_pair("n_positive", LogicalType::UBIGINT));
    children.push_back(make_pair("n_negative", LogicalType::UBIGINT));
    children.push_back(make_pair("n_unique_values", LogicalType::UBIGINT));
    children.push_back(make_pair("is_constant", LogicalType::BOOLEAN));
    children.push_back(make_pair("n_zeros_start", LogicalType::UBIGINT));
    children.push_back(make_pair("n_zeros_end", LogicalType::UBIGINT));
    children.push_back(make_pair("plateau_size", LogicalType::UBIGINT));
    children.push_back(make_pair("plateau_size_nonzero", LogicalType::UBIGINT));
    children.push_back(make_pair("mean", LogicalType::DOUBLE));
    children.push_back(make_pair("median", LogicalType::DOUBLE));
    children.push_back(make_pair("std_dev", LogicalType::DOUBLE));
    children.push_back(make_pair("variance", LogicalType::DOUBLE));
    children.push_back(make_pair("min", LogicalType::DOUBLE));
    children.push_back(make_pair("max", LogicalType::DOUBLE));
    children.push_back(make_pair("range", LogicalType::DOUBLE));
    children.push_back(make_pair("sum", LogicalType::DOUBLE));
    children.push_back(make_pair("skewness", LogicalType::DOUBLE));
    children.push_back(make_pair("kurtosis", LogicalType::DOUBLE));
    children.push_back(make_pair("tail_index", LogicalType::DOUBLE));
    children.push_back(make_pair("bimodality_coef", LogicalType::DOUBLE));
    children.push_back(make_pair("trimmed_mean", LogicalType::DOUBLE));
    children.push_back(make_pair("coef_variation", LogicalType::DOUBLE));
    children.push_back(make_pair("q1", LogicalType::DOUBLE));
    children.push_back(make_pair("q3", LogicalType::DOUBLE));
    children.push_back(make_pair("iqr", LogicalType::DOUBLE));
    children.push_back(make_pair("autocorr_lag1", LogicalType::DOUBLE));
    children.push_back(make_pair("trend_strength", LogicalType::DOUBLE));
    children.push_back(make_pair("seasonality_strength", LogicalType::DOUBLE));
    children.push_back(make_pair("entropy", LogicalType::DOUBLE));
    children.push_back(make_pair("stability", LogicalType::DOUBLE));
    return LogicalType::STRUCT(std::move(children));
}

// Extract values from a LIST vector into a flat array
static void ExtractListValues(Vector &list_vec, idx_t row_idx,
                              vector<double> &out_values,
                              vector<uint64_t> &out_validity) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_validity.clear();

    idx_t list_size = list_entry.length;
    idx_t list_offset = list_entry.offset;

    // Reserve space
    out_values.resize(list_size);
    size_t validity_words = (list_size + 63) / 64;
    out_validity.resize(validity_words, 0);

    for (idx_t i = 0; i < list_size; i++) {
        idx_t child_idx = list_offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values[i] = child_data[child_idx];
            // Set validity bit
            out_validity[i / 64] |= (1ULL << (i % 64));
        } else {
            out_values[i] = 0.0; // Placeholder for NULL
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

    // Process each row
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        // Check if input is NULL
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Extract values from the list
        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, row_idx, values, validity);

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

    ts_stats_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetTsStatsResultType(),
        TsStatsFunction
    ));

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_stats_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

} // namespace duckdb
