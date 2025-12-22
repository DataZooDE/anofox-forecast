#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

static void ExtractListAsDouble(Vector &list_vec, idx_t row_idx, vector<double> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values.push_back(child_data[child_idx]);
        }
    }
}

// ============================================================================
// ts_diff - Compute differences
// ============================================================================

static void TsDiffFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &order_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat order_data;
    order_vec.ToUnifiedFormat(count, order_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        // Get order from unified format (handles constant vectors correctly)
        auto order_idx = order_data.sel->get_index(row_idx);
        int32_t order = 1;
        if (order_data.validity.RowIsValid(order_idx)) {
            order = UnifiedVectorFormat::GetData<int32_t>(order_data)[order_idx];
        }

        double *out_values = nullptr;
        size_t out_length = 0;
        AnofoxError error;

        bool success = anofox_ts_diff(
            values.data(),
            values.size(),
            order,
            &out_values,
            &out_length,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_diff failed: %s", error.message);
        }

        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = out_length;

        ListVector::Reserve(result, current_size + out_length);
        ListVector::SetListSize(result, current_size + out_length);

        auto child_data = FlatVector::GetData<double>(list_child);
        if (out_values && out_length > 0) {
            memcpy(child_data + current_size, out_values, out_length * sizeof(double));
            anofox_free_double_array(out_values);
        }
    }
}

void RegisterTsDiffFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_diff_set("ts_diff");
    ts_diff_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDiffFunction
    ));
    loader.RegisterFunction(ts_diff_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_diff");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDiffFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_drop_constant - Filter out constant series
// ============================================================================

static void TsDropConstantFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        // Check if constant
        bool is_constant = true;
        if (values.size() > 1) {
            double first = values[0];
            for (size_t i = 1; i < values.size(); i++) {
                if (std::abs(values[i] - first) > 1e-10) {
                    is_constant = false;
                    break;
                }
            }
        }

        if (is_constant) {
            FlatVector::SetNull(result, row_idx, true);
        } else {
            // Copy through the values
            auto list_data = FlatVector::GetData<list_entry_t>(result);
            auto &list_child = ListVector::GetEntry(result);
            auto current_size = ListVector::GetListSize(result);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = values.size();

            ListVector::Reserve(result, current_size + values.size());
            ListVector::SetListSize(result, current_size + values.size());

            auto child_data = FlatVector::GetData<double>(list_child);
            memcpy(child_data + current_size, values.data(), values.size() * sizeof(double));
        }
    }
}

void RegisterTsDropConstantFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_drop_set("ts_drop_constant");
    ts_drop_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropConstantFunction
    ));
    loader.RegisterFunction(ts_drop_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_drop_constant");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropConstantFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_drop_short - Filter out short series
// ============================================================================

static void TsDropShortFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &min_len_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat min_len_data;
    min_len_vec.ToUnifiedFormat(count, min_len_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        // Get min_len from unified format (handles constant vectors correctly)
        auto min_len_idx = min_len_data.sel->get_index(row_idx);
        int32_t min_len = 10;
        if (min_len_data.validity.RowIsValid(min_len_idx)) {
            min_len = UnifiedVectorFormat::GetData<int32_t>(min_len_data)[min_len_idx];
        }

        if ((int32_t)values.size() < min_len) {
            FlatVector::SetNull(result, row_idx, true);
        } else {
            auto list_data = FlatVector::GetData<list_entry_t>(result);
            auto &list_child = ListVector::GetEntry(result);
            auto current_size = ListVector::GetListSize(result);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = values.size();

            ListVector::Reserve(result, current_size + values.size());
            ListVector::SetListSize(result, current_size + values.size());

            auto child_data = FlatVector::GetData<double>(list_child);
            memcpy(child_data + current_size, values.data(), values.size() * sizeof(double));
        }
    }
}

void RegisterTsDropShortFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_drop_set("ts_drop_short");
    ts_drop_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropShortFunction
    ));
    loader.RegisterFunction(ts_drop_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_drop_short");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropShortFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_drop_leading_zeros
// ============================================================================

static void TsDropLeadingZerosFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        // Find first non-zero
        size_t start = 0;
        for (size_t i = 0; i < values.size(); i++) {
            if (std::abs(values[i]) > 1e-10) {
                start = i;
                break;
            }
        }

        size_t new_len = values.size() - start;

        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = new_len;

        ListVector::Reserve(result, current_size + new_len);
        ListVector::SetListSize(result, current_size + new_len);

        auto child_data = FlatVector::GetData<double>(list_child);
        if (new_len > 0) {
            memcpy(child_data + current_size, values.data() + start, new_len * sizeof(double));
        }
    }
}

void RegisterTsDropLeadingZerosFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_drop_set("ts_drop_leading_zeros");
    ts_drop_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropLeadingZerosFunction
    ));
    loader.RegisterFunction(ts_drop_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_drop_leading_zeros");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropLeadingZerosFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_drop_trailing_zeros
// ============================================================================

static void TsDropTrailingZerosFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        // Find last non-zero
        size_t end = values.size();
        for (int i = values.size() - 1; i >= 0; i--) {
            if (std::abs(values[i]) > 1e-10) {
                end = i + 1;
                break;
            }
        }

        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = end;

        ListVector::Reserve(result, current_size + end);
        ListVector::SetListSize(result, current_size + end);

        auto child_data = FlatVector::GetData<double>(list_child);
        if (end > 0) {
            memcpy(child_data + current_size, values.data(), end * sizeof(double));
        }
    }
}

void RegisterTsDropTrailingZerosFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_drop_set("ts_drop_trailing_zeros");
    ts_drop_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropTrailingZerosFunction
    ));
    loader.RegisterFunction(ts_drop_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_drop_trailing_zeros");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropTrailingZerosFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ============================================================================
// ts_drop_edge_zeros
// ============================================================================

static void TsDropEdgeZerosFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        // Find first and last non-zero
        size_t start = 0;
        for (size_t i = 0; i < values.size(); i++) {
            if (std::abs(values[i]) > 1e-10) {
                start = i;
                break;
            }
        }

        size_t end = values.size();
        for (int i = values.size() - 1; i >= (int)start; i--) {
            if (std::abs(values[i]) > 1e-10) {
                end = i + 1;
                break;
            }
        }

        size_t new_len = (end > start) ? (end - start) : 0;

        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);
        auto current_size = ListVector::GetListSize(result);

        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = new_len;

        ListVector::Reserve(result, current_size + new_len);
        ListVector::SetListSize(result, current_size + new_len);

        auto child_data = FlatVector::GetData<double>(list_child);
        if (new_len > 0) {
            memcpy(child_data + current_size, values.data() + start, new_len * sizeof(double));
        }
    }
}

void RegisterTsDropEdgeZerosFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_drop_set("ts_drop_edge_zeros");
    ts_drop_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropEdgeZerosFunction
    ));
    loader.RegisterFunction(ts_drop_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_drop_edge_zeros");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::LIST(LogicalType::DOUBLE),
        TsDropEdgeZerosFunction
    ));
    loader.RegisterFunction(anofox_set);
}

} // namespace duckdb
