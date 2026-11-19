#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_forecast_params.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/common/types/vector.hpp"
#include <cstring>
#include <unordered_set>

namespace duckdb {

// ============================================================================
// _ts_forecast_inspect_scalar / _ts_forecast_explain_scalar
//
// Fit the requested model and return a JSON snapshot of its state.
//
// - inspect: model fit-state (Inspectable::explanation → per-family payload)
//   Signature:
//     _ts_forecast_inspect_scalar(values LIST(DOUBLE), method VARCHAR, params ANY)
//     → VARCHAR (JSON)
//
// - explain: per-horizon decomposition (Explainable::explain(horizon))
//   Signature:
//     _ts_forecast_explain_scalar(values LIST(DOUBLE), horizon INTEGER,
//                                 method VARCHAR, params ANY)
//     → VARCHAR (JSON)
//
// Both are the per-group workhorses behind the ts_forecast_inspect_by /
// ts_forecast_explain_by macros. The macros unpack the JSON into a wide
// STRUCT via struct_pack + json_extract on the C++ side, so the FFI wire
// stays a single string.
// ============================================================================

// Params go through the same shared parser/writer as ts_forecast_by and
// ts_forecast_agg (ts_forecast_params.hpp): closed key set, 'model' only with
// method='ETS', every valid key applied. A local subset parser used to sit
// here and silently ignored unknown or inapplicable keys (#315).
static void FillForecastOptions(ForecastOptions &opts,
                                 const string &method,
                                 const Value &params_val,
                                 int32_t horizon) {
    ForecastParamSet p;
    if (!params_val.IsNull()) {
        p = ParseForecastParamSet(params_val);
    }
    memset(&opts, 0, sizeof(opts));
    ApplyForecastParamSet(p, method, opts);
    opts.horizon = horizon;
    opts.include_fitted = false;
    opts.include_residuals = false;
}

// Extract a LIST(DOUBLE) row into a flat (values, validity) pair suitable
// for the FFI call. Returns false on empty / null lists.
static bool BuildSeriesFromValueList(Vector &value_list_vec,
                                     UnifiedVectorFormat &value_list_data,
                                     idx_t row_idx,
                                     vector<double> &out_values,
                                     vector<uint64_t> &out_validity) {
    auto value_idx = value_list_data.sel->get_index(row_idx);
    if (!value_list_data.validity.RowIsValid(value_idx)) return false;

    auto value_entries = UnifiedVectorFormat::GetData<list_entry_t>(value_list_data);
    auto &value_entry = value_entries[value_idx];
    auto &value_child = ListChildMut(value_list_vec);

    UnifiedVectorFormat value_child_data;
    ToUnified(value_child, ListVector::GetListSize(value_list_vec), value_child_data);
    auto value_values = UnifiedVectorFormat::GetData<double>(value_child_data);

    idx_t n = value_entry.length;
    if (n == 0) return false;

    out_values.assign(n, 0.0);
    size_t words = (n + 63) / 64;
    out_validity.assign(words, 0);

    for (idx_t i = 0; i < n; i++) {
        idx_t child_idx = value_entry.offset + i;
        auto unified_idx = value_child_data.sel->get_index(child_idx);
        if (value_child_data.validity.RowIsValid(unified_idx)) {
            out_values[i] = value_values[unified_idx];
            out_validity[i / 64] |= (1ULL << (i % 64));
        }
    }
    return true;
}

// ============================================================================
// _ts_forecast_inspect_scalar
// ============================================================================

static void TsForecastInspectScalarExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    idx_t count = args.size();

    auto &value_list_vec = args.data[0];
    auto &method_vec = args.data[1];
    auto &params_vec = args.data[2];

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat value_list_data, method_data, params_data;
    ToUnified(value_list_vec, count, value_list_data);
    ToUnified(method_vec, count, method_data);
    ToUnified(params_vec, count, params_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        vector<double> values;
        vector<uint64_t> validity;
        if (!BuildSeriesFromValueList(value_list_vec, value_list_data, row_idx, values, validity)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto m_idx = method_data.sel->get_index(row_idx);
        string method = "AutoETS";
        if (method_data.validity.RowIsValid(m_idx)) {
            method = UnifiedVectorFormat::GetData<string_t>(method_data)[m_idx].GetString();
        }

        Value params_val;
        auto p_idx = params_data.sel->get_index(row_idx);
        if (params_data.validity.RowIsValid(p_idx)) {
            params_val = NormalizeParamsValue(params_vec.GetValue(row_idx));
        }

        ForecastOptions opts;
        FillForecastOptions(opts, method, params_val, /*horizon*/ 1);

        char *out_json = nullptr;
        AnofoxError error;
        bool success = anofox_ts_forecast_inspect(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            &opts,
            &out_json,
            &error);

        if (!success) {
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        string json_out = out_json ? string(out_json) : string();
        if (out_json) {
            anofox_free_string(out_json);
        }
        result.SetValue(row_idx, Value(std::move(json_out)));
    }
}

// ============================================================================
// _ts_forecast_explain_scalar
// ============================================================================

static void TsForecastExplainScalarExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    idx_t count = args.size();

    auto &value_list_vec = args.data[0];
    auto &horizon_vec = args.data[1];
    auto &method_vec = args.data[2];
    auto &params_vec = args.data[3];

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat value_list_data, horizon_data, method_data, params_data;
    ToUnified(value_list_vec, count, value_list_data);
    ToUnified(horizon_vec, count, horizon_data);
    ToUnified(method_vec, count, method_data);
    ToUnified(params_vec, count, params_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        vector<double> values;
        vector<uint64_t> validity;
        if (!BuildSeriesFromValueList(value_list_vec, value_list_data, row_idx, values, validity)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto h_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = 12;
        if (horizon_data.validity.RowIsValid(h_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[h_idx];
        }
        if (horizon <= 0) {
            throw InvalidInputException("horizon must be >= 1 (got %d)", static_cast<int>(horizon));
        }

        auto m_idx = method_data.sel->get_index(row_idx);
        string method = "ETS";
        if (method_data.validity.RowIsValid(m_idx)) {
            method = UnifiedVectorFormat::GetData<string_t>(method_data)[m_idx].GetString();
        }

        Value params_val;
        auto p_idx = params_data.sel->get_index(row_idx);
        if (params_data.validity.RowIsValid(p_idx)) {
            params_val = NormalizeParamsValue(params_vec.GetValue(row_idx));
        }

        ForecastOptions opts;
        FillForecastOptions(opts, method, params_val, horizon);

        char *out_json = nullptr;
        AnofoxError error;
        bool success = anofox_ts_forecast_explain(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            static_cast<size_t>(horizon),
            &opts,
            &out_json,
            &error);

        if (!success) {
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        string json_out = out_json ? string(out_json) : string();
        if (out_json) {
            anofox_free_string(out_json);
        }
        result.SetValue(row_idx, Value(std::move(json_out)));
    }
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsForecastInspectScalarFunction(ExtensionLoader &loader) {
    // _ts_forecast_inspect_scalar(values LIST(DOUBLE), method VARCHAR, params ANY)
    //   → VARCHAR (JSON)
    ScalarFunction inspect_func("_ts_forecast_inspect_scalar",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR, LogicalType::ANY},
        LogicalType::VARCHAR,
        TsForecastInspectScalarExecute);
    MarkFallible(inspect_func);
    SetNullHandlingOf(inspect_func, FunctionNullHandling::SPECIAL_HANDLING);
    loader.RegisterFunction(inspect_func);

    // _ts_forecast_explain_scalar(values LIST(DOUBLE), horizon INT,
    //                             method VARCHAR, params ANY) → VARCHAR (JSON)
    ScalarFunction explain_func("_ts_forecast_explain_scalar",
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER,
         LogicalType::VARCHAR, LogicalType::ANY},
        LogicalType::VARCHAR,
        TsForecastExplainScalarExecute);
    MarkFallible(explain_func);
    SetNullHandlingOf(explain_func, FunctionNullHandling::SPECIAL_HANDLING);
    loader.RegisterFunction(explain_func);
}

} // namespace duckdb
