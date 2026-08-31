// Phase 6 (INSP-01): Ensemble member introspection scalar functions
//
// Implements _ts_ensemble_inspect_native and _ts_auto_ensemble_inspect_native
// as ScalarFunctions following the Phase 5 _ts_forecast_ensemble_native precedent.
//
// _ts_ensemble_inspect_native signature:
//   (values LIST(DOUBLE), members LIST(VARCHAR), combination_method VARCHAR,
//    seasonal_period INTEGER)
//   -> LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
//
// _ts_auto_ensemble_inspect_native signature:
//   (values LIST(DOUBLE), top_k INTEGER, combination_method VARCHAR,
//    seasonal_period INTEGER)
//   -> LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
//
// Both functions are registered via RegisterTsEnsembleInspectNativeFunction,
// dispatched per-series from ts_ensemble_inspect_by / ts_auto_ensemble_inspect_by
// macros via: SELECT group_col, unnest(fn(...), recursive := true) GROUP BY group_col.

#include "ts_ensemble_inspect_native.hpp"
#include "anofox_forecast_extension.hpp"    // ExtensionLoader
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/common/types/vector.hpp"
#include <cstring>

namespace duckdb {

// ============================================================================
// Shared: return type for both introspection functions
// ============================================================================
//
// LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
//   - member_name: model name (e.g. "AutoARIMA", "AutoETS", "Naive")
//   - weight: combination weight (NULL for AutoEnsemble non-Mean)
//   - score: in-sample MSE (NULL for explicit-member inspect)
//
static LogicalType MakeInspectReturnType() {
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("member_name", LogicalType::VARCHAR));
    struct_children.push_back(make_pair("weight",      LogicalType::DOUBLE));
    struct_children.push_back(make_pair("score",       LogicalType::DOUBLE));
    return LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));
}

// ============================================================================
// _ts_ensemble_inspect_native — explicit-member ensemble introspection
// ============================================================================

struct TsEnsembleInspectNativeBindData : public FunctionData {
    string combination_method = "";
    int64_t seasonal_period = 0;

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<TsEnsembleInspectNativeBindData>();
        copy->combination_method = combination_method;
        copy->seasonal_period = seasonal_period;
        return std::move(copy);
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsEnsembleInspectNativeBindData>();
        return combination_method == other.combination_method &&
               seasonal_period == other.seasonal_period;
    }
};

static unique_ptr<FunctionData> TsEnsembleInspectNativeBind(
    ClientContext &,
    ScalarFunction &bound_function,
    vector<unique_ptr<Expression>> &) {

    auto bind_data = make_uniq<TsEnsembleInspectNativeBindData>();
    bound_function.return_type = MakeInspectReturnType();
    return std::move(bind_data);
}

static void TsEnsembleInspectNativeExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &bind_data = state.expr.Cast<BoundFunctionExpression>()
        .bind_info->Cast<TsEnsembleInspectNativeBindData>();
    idx_t count = args.size();

    auto &value_list_vec    = args.data[0];  // LIST(DOUBLE) values
    auto &members_list_vec  = args.data[1];  // LIST(VARCHAR) members
    auto &method_vec        = args.data[2];  // VARCHAR combination_method
    auto &period_vec        = args.data[3];  // INTEGER seasonal_period

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat value_list_data, members_list_data, method_data, period_data;
    value_list_vec.ToUnifiedFormat(count, value_list_data);
    members_list_vec.ToUnifiedFormat(count, members_list_data);
    method_vec.ToUnifiedFormat(count, method_data);
    period_vec.ToUnifiedFormat(count, period_data);

    // Precompute result struct type for STRUCT value construction
    child_list_t<LogicalType> struct_type;
    struct_type.push_back(make_pair("member_name", LogicalType::VARCHAR));
    struct_type.push_back(make_pair("weight",      LogicalType::DOUBLE));
    struct_type.push_back(make_pair("score",       LogicalType::DOUBLE));

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto value_idx  = value_list_data.sel->get_index(row_idx);
        auto member_idx = members_list_data.sel->get_index(row_idx);

        if (!value_list_data.validity.RowIsValid(value_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Extract members LIST(VARCHAR) ---
        vector<string> member_names;
        if (!members_list_data.validity.RowIsValid(member_idx)) {
            throw InvalidInputException(
                "ts_ensemble_inspect_by: 'members' must not be NULL. "
                "Provide a non-empty VARCHAR[] list, e.g., members := ['AutoARIMA','AutoETS','Naive'].");
        }
        {
            auto member_entries = UnifiedVectorFormat::GetData<list_entry_t>(members_list_data);
            auto &member_entry  = member_entries[member_idx];
            auto &member_child  = ListVector::GetEntry(members_list_vec);
            idx_t m_off = member_entry.offset;
            idx_t m_len = member_entry.length;
            for (idx_t mi = 0; mi < m_len; mi++) {
                Value mv = member_child.GetValue(m_off + mi);
                if (!mv.IsNull()) {
                    member_names.push_back(StringValue::Get(mv));
                }
            }
        }
        if (member_names.size() < 2) {
            throw InvalidInputException(
                "ts_ensemble_inspect_by: at least 2 members are required. Got %zu.",
                member_names.size());
        }

        // --- Extract values from LIST(DOUBLE) ---
        auto value_entries = UnifiedVectorFormat::GetData<list_entry_t>(value_list_data);
        auto &value_entry  = value_entries[value_idx];
        auto &value_child  = ListVector::GetEntry(value_list_vec);

        UnifiedVectorFormat value_child_data;
        value_child.ToUnifiedFormat(ListVector::GetListSize(value_list_vec), value_child_data);
        auto value_values = UnifiedVectorFormat::GetData<double>(value_child_data);

        idx_t n      = value_entry.length;
        idx_t offset = value_entry.offset;

        if (n == 0) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build values + validity (no date sorting needed — ORDER BY in macro)
        vector<double> sorted_values(n);
        size_t validity_words = (n + 63) / 64;
        vector<uint64_t> validity_bits(validity_words, 0);

        for (idx_t i = 0; i < n; i++) {
            idx_t child_idx  = offset + i;
            auto  unified_i  = value_child_data.sel->get_index(child_idx);
            if (value_child_data.validity.RowIsValid(unified_i)) {
                sorted_values[i] = value_values[unified_i];
                validity_bits[i / 64] |= (1ULL << (i % 64));
            } else {
                sorted_values[i] = 0.0;
            }
        }

        // --- Parse per-row parameters ---
        auto m_idx = method_data.sel->get_index(row_idx);
        string combination_method = bind_data.combination_method;
        if (method_data.validity.RowIsValid(m_idx)) {
            combination_method = UnifiedVectorFormat::GetData<string_t>(method_data)[m_idx].GetString();
        }

        auto p_idx = period_data.sel->get_index(row_idx);
        int32_t seasonal_period = static_cast<int32_t>(bind_data.seasonal_period);
        if (period_data.validity.RowIsValid(p_idx)) {
            seasonal_period = UnifiedVectorFormat::GetData<int32_t>(period_data)[p_idx];
        }

        // --- Build null-delimited member buffer for FFI ---
        std::string members_buf;
        for (const auto &name : member_names) {
            members_buf += name;
            members_buf += '\0';
        }
        size_t members_count = member_names.size();

        // --- Call Rust FFI ---
        EnsembleInspectResult insp_result;
        memset(&insp_result, 0, sizeof(insp_result));
        AnofoxError error;
        memset(&error, 0, sizeof(error));

        bool success = anofox_ts_ensemble_inspect(
            sorted_values.data(),
            validity_bits.empty() ? nullptr : validity_bits.data(),
            sorted_values.size(),
            members_buf.data(),
            members_buf.size(),       // total bytes including NULs
            members_count,
            combination_method.c_str(),
            static_cast<int>(seasonal_period),
            &insp_result,
            &error
        );

        if (!success) {
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }
            // Computation/data errors: emit NULL for this row
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Unpack EnsembleInspectResult → LIST(STRUCT(member_name, weight, score)) ---
        // Parse null-delimited names buffer
        vector<string> names_out;
        if (insp_result.member_names_buf != nullptr && insp_result.member_names_buf_len > 0) {
            const char *buf = insp_result.member_names_buf;
            size_t remaining = insp_result.member_names_buf_len;
            const char *p = buf;
            while (p < buf + remaining) {
                size_t name_len = strnlen(p, remaining - (p - buf));
                if (name_len > 0) {
                    names_out.emplace_back(p, name_len);
                }
                p += name_len + 1; // skip past NUL
            }
        }

        vector<Value> member_structs;
        member_structs.reserve(insp_result.count);

        for (size_t i = 0; i < insp_result.count; i++) {
            string name_i = (i < names_out.size()) ? names_out[i] : "";

            // weight: Some if weights ptr non-null; NULL for AutoEnsemble non-Mean
            Value weight_val = (insp_result.weights != nullptr)
                ? Value::DOUBLE(insp_result.weights[i])
                : Value(LogicalType::DOUBLE);  // NULL

            // score: explicit-member has scores=null
            Value score_val = (insp_result.scores != nullptr)
                ? Value::DOUBLE(insp_result.scores[i])
                : Value(LogicalType::DOUBLE);  // NULL

            child_list_t<Value> struct_values;
            struct_values.push_back(make_pair("member_name", Value(name_i)));
            struct_values.push_back(make_pair("weight",      weight_val));
            struct_values.push_back(make_pair("score",       score_val));
            member_structs.push_back(Value::STRUCT(std::move(struct_values)));
        }

        anofox_free_ensemble_inspect_result(&insp_result);

        result.SetValue(row_idx, Value::LIST(std::move(member_structs)));
    }
}

// ============================================================================
// _ts_auto_ensemble_inspect_native — AutoEnsemble introspection
// ============================================================================

struct TsAutoEnsembleInspectNativeBindData : public FunctionData {
    int64_t top_k = 3;
    string combination_method = "";
    int64_t seasonal_period = 0;

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<TsAutoEnsembleInspectNativeBindData>();
        copy->top_k = top_k;
        copy->combination_method = combination_method;
        copy->seasonal_period = seasonal_period;
        return std::move(copy);
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsAutoEnsembleInspectNativeBindData>();
        return top_k == other.top_k &&
               combination_method == other.combination_method &&
               seasonal_period == other.seasonal_period;
    }
};

static unique_ptr<FunctionData> TsAutoEnsembleInspectNativeBind(
    ClientContext &,
    ScalarFunction &bound_function,
    vector<unique_ptr<Expression>> &) {

    auto bind_data = make_uniq<TsAutoEnsembleInspectNativeBindData>();
    bound_function.return_type = MakeInspectReturnType();
    return std::move(bind_data);
}

static void TsAutoEnsembleInspectNativeExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &bind_data = state.expr.Cast<BoundFunctionExpression>()
        .bind_info->Cast<TsAutoEnsembleInspectNativeBindData>();
    idx_t count = args.size();

    auto &value_list_vec = args.data[0];  // LIST(DOUBLE) values
    auto &top_k_vec      = args.data[1];  // INTEGER top_k
    auto &method_vec     = args.data[2];  // VARCHAR combination_method
    auto &period_vec     = args.data[3];  // INTEGER seasonal_period

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat value_list_data, top_k_data, method_data, period_data;
    value_list_vec.ToUnifiedFormat(count, value_list_data);
    top_k_vec.ToUnifiedFormat(count, top_k_data);
    method_vec.ToUnifiedFormat(count, method_data);
    period_vec.ToUnifiedFormat(count, period_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto value_idx = value_list_data.sel->get_index(row_idx);

        if (!value_list_data.validity.RowIsValid(value_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Extract values from LIST(DOUBLE) ---
        auto value_entries = UnifiedVectorFormat::GetData<list_entry_t>(value_list_data);
        auto &value_entry  = value_entries[value_idx];
        auto &value_child  = ListVector::GetEntry(value_list_vec);

        UnifiedVectorFormat value_child_data;
        value_child.ToUnifiedFormat(ListVector::GetListSize(value_list_vec), value_child_data);
        auto value_values = UnifiedVectorFormat::GetData<double>(value_child_data);

        idx_t n      = value_entry.length;
        idx_t offset = value_entry.offset;

        if (n == 0) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build values + validity
        vector<double> sorted_values(n);
        size_t validity_words = (n + 63) / 64;
        vector<uint64_t> validity_bits(validity_words, 0);

        for (idx_t i = 0; i < n; i++) {
            idx_t child_idx = offset + i;
            auto  unified_i = value_child_data.sel->get_index(child_idx);
            if (value_child_data.validity.RowIsValid(unified_i)) {
                sorted_values[i] = value_values[unified_i];
                validity_bits[i / 64] |= (1ULL << (i % 64));
            } else {
                sorted_values[i] = 0.0;
            }
        }

        // --- Parse per-row parameters ---
        auto tk_idx = top_k_data.sel->get_index(row_idx);
        int32_t top_k = static_cast<int32_t>(bind_data.top_k);
        if (top_k_data.validity.RowIsValid(tk_idx)) {
            top_k = UnifiedVectorFormat::GetData<int32_t>(top_k_data)[tk_idx];
        }

        auto m_idx = method_data.sel->get_index(row_idx);
        string combination_method = bind_data.combination_method;
        if (method_data.validity.RowIsValid(m_idx)) {
            combination_method = UnifiedVectorFormat::GetData<string_t>(method_data)[m_idx].GetString();
        }

        auto p_idx = period_data.sel->get_index(row_idx);
        int32_t seasonal_period = static_cast<int32_t>(bind_data.seasonal_period);
        if (period_data.validity.RowIsValid(p_idx)) {
            seasonal_period = UnifiedVectorFormat::GetData<int32_t>(period_data)[p_idx];
        }

        // --- Call Rust FFI (no members buffer for AutoEnsemble) ---
        EnsembleInspectResult insp_result;
        memset(&insp_result, 0, sizeof(insp_result));
        AnofoxError error;
        memset(&error, 0, sizeof(error));

        bool success = anofox_ts_auto_ensemble_inspect(
            sorted_values.data(),
            validity_bits.empty() ? nullptr : validity_bits.data(),
            sorted_values.size(),
            static_cast<int>(top_k),
            combination_method.c_str(),
            static_cast<int>(seasonal_period),
            &insp_result,
            &error
        );

        if (!success) {
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Unpack EnsembleInspectResult → LIST(STRUCT(member_name, weight, score)) ---
        vector<string> names_out;
        if (insp_result.member_names_buf != nullptr && insp_result.member_names_buf_len > 0) {
            const char *buf = insp_result.member_names_buf;
            size_t remaining = insp_result.member_names_buf_len;
            const char *p = buf;
            while (p < buf + remaining) {
                size_t name_len = strnlen(p, remaining - (p - buf));
                if (name_len > 0) {
                    names_out.emplace_back(p, name_len);
                }
                p += name_len + 1;
            }
        }

        vector<Value> member_structs;
        member_structs.reserve(insp_result.count);

        for (size_t i = 0; i < insp_result.count; i++) {
            string name_i = (i < names_out.size()) ? names_out[i] : "";

            // weight: Some if weights ptr non-null (Mean combination); NULL for non-Mean
            Value weight_val = (insp_result.weights != nullptr)
                ? Value::DOUBLE(insp_result.weights[i])
                : Value(LogicalType::DOUBLE);  // NULL

            // score: MSE from all_scores() — always present for AutoEnsemble
            Value score_val = (insp_result.scores != nullptr)
                ? Value::DOUBLE(insp_result.scores[i])
                : Value(LogicalType::DOUBLE);  // NULL (should not happen for AutoEnsemble)

            child_list_t<Value> struct_values;
            struct_values.push_back(make_pair("member_name", Value(name_i)));
            struct_values.push_back(make_pair("weight",      weight_val));
            struct_values.push_back(make_pair("score",       score_val));
            member_structs.push_back(Value::STRUCT(std::move(struct_values)));
        }

        anofox_free_ensemble_inspect_result(&insp_result);

        result.SetValue(row_idx, Value::LIST(std::move(member_structs)));
    }
}

// ============================================================================
// Registration — both ScalarFunctions in one call
// ============================================================================

void RegisterTsEnsembleInspectNativeFunction(ExtensionLoader &loader) {
    // _ts_ensemble_inspect_native — explicit-member ensemble introspection
    //   (values LIST(DOUBLE), members LIST(VARCHAR), combination_method VARCHAR,
    //    seasonal_period INTEGER)
    //   -> LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
    ScalarFunction explicit_func(
        "_ts_ensemble_inspect_native",
        {LogicalType::LIST(LogicalType::DOUBLE),   // values
         LogicalType::LIST(LogicalType::VARCHAR),  // members
         LogicalType::VARCHAR,                      // combination_method
         LogicalType::INTEGER},                     // seasonal_period
        LogicalType::LIST(LogicalType::ANY),        // return type set at bind time
        TsEnsembleInspectNativeExecute,
        TsEnsembleInspectNativeBind);
    explicit_func.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    loader.RegisterFunction(explicit_func);

    // _ts_auto_ensemble_inspect_native — AutoEnsemble introspection
    //   (values LIST(DOUBLE), top_k INTEGER, combination_method VARCHAR,
    //    seasonal_period INTEGER)
    //   -> LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
    ScalarFunction auto_func(
        "_ts_auto_ensemble_inspect_native",
        {LogicalType::LIST(LogicalType::DOUBLE),   // values
         LogicalType::INTEGER,                      // top_k
         LogicalType::VARCHAR,                      // combination_method
         LogicalType::INTEGER},                     // seasonal_period
        LogicalType::LIST(LogicalType::ANY),        // return type set at bind time
        TsAutoEnsembleInspectNativeExecute,
        TsAutoEnsembleInspectNativeBind);
    auto_func.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    loader.RegisterFunction(auto_func);
}

} // namespace duckdb
