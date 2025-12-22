#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

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

    out_values.resize(list_size);
    size_t validity_words = (list_size + 63) / 64;
    out_validity.resize(validity_words, 0);

    for (idx_t i = 0; i < list_size; i++) {
        idx_t child_idx = list_offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values[i] = child_data[child_idx];
            out_validity[i / 64] |= (1ULL << (i % 64));
        } else {
            out_values[i] = 0.0;
        }
    }
}

// ============================================================================
// ts_fill_nulls_const - Fill NULLs with constant
// ============================================================================

static void TsFillNullsConstFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &const_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat fill_data;
    const_vec.ToUnifiedFormat(count, fill_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, row_idx, values, validity);

        // Get fill_value from unified format (handles constant vectors correctly)
        auto fill_idx = fill_data.sel->get_index(row_idx);
        double fill_value = 0.0;
        if (fill_data.validity.RowIsValid(fill_idx)) {
            fill_value = UnifiedVectorFormat::GetData<double>(fill_data)[fill_idx];
        }

        double *out_values = nullptr;
        AnofoxError error;

        bool success = anofox_ts_fill_nulls_const(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            fill_value,
            &out_values,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_fill_nulls_const failed: %s", error.message);
        }

        // Build result list
        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = values.size();

        ListVector::Reserve(result, current_size + values.size());
        ListVector::SetListSize(result, current_size + values.size());

        auto child_data = FlatVector::GetData<double>(list_child);
        if (out_values) {
            memcpy(child_data + current_size, out_values, values.size() * sizeof(double));
            anofox_free_double_array(out_values);
        }
    }
}

void RegisterTsFillNullsConstFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_fill_set("ts_fill_nulls_const");
    ts_fill_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsConstFunction
    ));
    loader.RegisterFunction(ts_fill_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_fill_nulls_const");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsConstFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_fill_nulls_forward - Forward fill
// ============================================================================

static void TsFillNullsForwardFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, row_idx, values, validity);

        // Simple forward fill implementation in C++
        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = values.size();

        ListVector::Reserve(result, current_size + values.size());
        ListVector::SetListSize(result, current_size + values.size());

        auto child_data = FlatVector::GetData<double>(list_child);
        auto &child_validity = FlatVector::Validity(list_child);

        double last_valid = 0.0;
        bool has_valid = false;

        for (size_t i = 0; i < values.size(); i++) {
            size_t word_idx = i / 64;
            size_t bit_idx = i % 64;
            bool is_valid = (validity.size() > word_idx) && ((validity[word_idx] >> bit_idx) & 1);

            if (is_valid) {
                last_valid = values[i];
                has_valid = true;
                child_data[current_size + i] = values[i];
            } else if (has_valid) {
                child_data[current_size + i] = last_valid;
            } else {
                child_data[current_size + i] = 0.0;
                child_validity.SetInvalid(current_size + i);
            }
        }
    }
}

void RegisterTsFillNullsForwardFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_fill_set("ts_fill_nulls_forward");
    ts_fill_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsForwardFunction
    ));
    loader.RegisterFunction(ts_fill_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_fill_nulls_forward");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsForwardFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_fill_nulls_backward - Backward fill
// ============================================================================

static void TsFillNullsBackwardFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, row_idx, values, validity);

        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = values.size();

        ListVector::Reserve(result, current_size + values.size());
        ListVector::SetListSize(result, current_size + values.size());

        auto child_data = FlatVector::GetData<double>(list_child);
        auto &child_validity = FlatVector::Validity(list_child);

        // First pass: copy values
        for (size_t i = 0; i < values.size(); i++) {
            child_data[current_size + i] = values[i];
        }

        // Backward fill
        double next_valid = 0.0;
        bool has_valid = false;

        for (int i = values.size() - 1; i >= 0; i--) {
            size_t word_idx = i / 64;
            size_t bit_idx = i % 64;
            bool is_valid = (validity.size() > word_idx) && ((validity[word_idx] >> bit_idx) & 1);

            if (is_valid) {
                next_valid = values[i];
                has_valid = true;
            } else if (has_valid) {
                child_data[current_size + i] = next_valid;
            } else {
                child_validity.SetInvalid(current_size + i);
            }
        }
    }
}

void RegisterTsFillNullsBackwardFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_fill_set("ts_fill_nulls_backward");
    ts_fill_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsBackwardFunction
    ));
    loader.RegisterFunction(ts_fill_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_fill_nulls_backward");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsBackwardFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_fill_nulls_mean - Fill with mean
// ============================================================================

static void TsFillNullsMeanFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, row_idx, values, validity);

        double *out_values = nullptr;
        AnofoxError error;

        bool success = anofox_ts_fill_nulls_mean(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            &out_values,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_fill_nulls_mean failed: %s", error.message);
        }

        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = values.size();

        ListVector::Reserve(result, current_size + values.size());
        ListVector::SetListSize(result, current_size + values.size());

        auto child_data = FlatVector::GetData<double>(list_child);
        if (out_values) {
            memcpy(child_data + current_size, out_values, values.size() * sizeof(double));
            anofox_free_double_array(out_values);
        }
    }
}

void RegisterTsFillNullsMeanFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_fill_set("ts_fill_nulls_mean");
    ts_fill_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsMeanFunction
    ));
    loader.RegisterFunction(ts_fill_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_fill_nulls_mean");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsFillNullsMeanFunction
    ));
    loader.RegisterFunction(anofox_set);
}

} // namespace duckdb
