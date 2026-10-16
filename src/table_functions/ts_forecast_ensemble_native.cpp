// Phase 5 (ENS-02): Explicit-member ensemble scalar function
//
// Implements _ts_forecast_ensemble_native — a ScalarFunction that follows the
// _ts_forecast_scalar precedent (per-series GROUP BY shape, unnest wrapper).
//
// Signature (two overloads — the 8-argument form adds confidence_level;
// the 7-argument form keeps the documented 0.90 default, plan 10-03):
//   _ts_forecast_ensemble_native(dates LIST, values LIST(DOUBLE),
//                                members LIST(VARCHAR), horizon INT,
//                                frequency VARCHAR, combination_method VARCHAR,
//                                seasonal_period INT[, confidence_level DOUBLE])
//   -> LIST(STRUCT(forecast_step INT, ds ANY, yhat DOUBLE,
//                  yhat_lower DOUBLE, yhat_upper DOUBLE, model_name VARCHAR))
//
// yhat_lower/yhat_upper are the crate Ensemble's own pooled prediction
// interval bounds at confidence_level (D-01, plan 10-03) — NULL only when
// the crate itself returns no bounds.
//
// Called per-group by the ts_forecast_ensemble_by macro via:
//   SELECT group_col, unnest(_ts_forecast_ensemble_native(...), recursive := true)
//   FROM source GROUP BY group_col
//
// DuckDB parallelizes the GROUP BY across cores natively.

#include "ts_forecast_ensemble_native.hpp"
#include "anofox_forecast_extension.hpp"    // ExtensionLoader
#include "ts_fill_gaps_native.hpp"          // ParseFrequencyWithType, DateColumnType, etc.
#include "ts_future_dates.hpp"              // StepFromFrequency, ComputeFutureDates (D-12)
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace duckdb {

// ============================================================================
// Bind Data
// ============================================================================

struct TsForecastEnsembleNativeBindData : public FunctionData {
    // Required
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;

    // Ensemble-specific
    vector<string> member_names;        // parsed from DuckDB LIST(VARCHAR)
    string combination_method = "";     // "" → "mean" in Rust core
    int64_t seasonal_period = 0;
    double confidence_level = 0.90;     // D-01, plan 10-03 — documented default

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<TsForecastEnsembleNativeBindData>();
        copy->horizon = horizon;
        copy->frequency_seconds = frequency_seconds;
        copy->frequency_is_raw = frequency_is_raw;
        copy->frequency_type = frequency_type;
        copy->member_names = member_names;
        copy->combination_method = combination_method;
        copy->seasonal_period = seasonal_period;
        copy->confidence_level = confidence_level;
        copy->date_col_type = date_col_type;
        return std::move(copy);
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsForecastEnsembleNativeBindData>();
        return horizon == other.horizon &&
               frequency_seconds == other.frequency_seconds &&
               member_names == other.member_names &&
               combination_method == other.combination_method &&
               seasonal_period == other.seasonal_period &&
               confidence_level == other.confidence_level;
    }
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsForecastEnsembleNativeBind(
    ClientContext &context,
    ScalarFunction &bound_function,
    vector<unique_ptr<Expression>> &arguments) {

    auto bind_data = make_uniq<TsForecastEnsembleNativeBindData>();

    // Detect date column type from LIST child type (argument 0 = dates)
    auto &date_list_type = arguments[0]->return_type;
    if (date_list_type.id() == LogicalTypeId::LIST) {
        auto &child_type = ListType::GetChildType(date_list_type);
        switch (child_type.id()) {
            case LogicalTypeId::DATE:
                bind_data->date_col_type = DateColumnType::DATE;
                break;
            case LogicalTypeId::TIMESTAMP:
            case LogicalTypeId::TIMESTAMP_TZ:
                bind_data->date_col_type = DateColumnType::TIMESTAMP;
                break;
            case LogicalTypeId::INTEGER:
                bind_data->date_col_type = DateColumnType::INTEGER;
                break;
            case LogicalTypeId::BIGINT:
                bind_data->date_col_type = DateColumnType::BIGINT;
                break;
            default:
                bind_data->date_col_type = DateColumnType::TIMESTAMP;
                break;
        }
    }

    // Extract members LIST(VARCHAR) from argument 2 at bind time if it is a constant.
    // Runtime extraction is done per-row in Execute.
    // If it is a constant, pre-validate here for early error reporting.
    if (arguments[2]->return_type.id() == LogicalTypeId::LIST) {
        // Members are extracted per-row at execute time; bind-time validation
        // for constant member lists happens via the constant-folding path.
        // No early extraction needed here — runtime extraction is reliable.
    }

    // Build the result struct type (matches _ts_forecast_scalar output)
    // Use the actual child type from the dates list for accurate ds type
    auto &date_child_type = (date_list_type.id() == LogicalTypeId::LIST)
        ? ListType::GetChildType(date_list_type)
        : LogicalType::TIMESTAMP;

    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("forecast_step", LogicalType::INTEGER));
    struct_children.push_back(make_pair("ds", date_child_type));
    struct_children.push_back(make_pair("yhat", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("yhat_lower", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("yhat_upper", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("model_name", LogicalType::VARCHAR));

    bound_function.return_type = LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));
    return std::move(bind_data);
}

// ============================================================================
// Execute Function
// ============================================================================

static void TsForecastEnsembleNativeExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &bind_data = state.expr.Cast<BoundFunctionExpression>().bind_info->Cast<TsForecastEnsembleNativeBindData>();
    idx_t count = args.size();

    auto &date_list_vec   = args.data[0];  // LIST(date)
    auto &value_list_vec  = args.data[1];  // LIST(DOUBLE)
    auto &members_list_vec = args.data[2]; // LIST(VARCHAR) — ensemble members
    auto &horizon_vec     = args.data[3];  // INTEGER
    auto &freq_vec        = args.data[4];  // VARCHAR
    auto &method_vec      = args.data[5];  // VARCHAR — combination_method
    auto &period_vec      = args.data[6];  // INTEGER — seasonal_period

    // confidence_level (D-01, plan 10-03): present only on the 8-argument
    // overload; the 7-argument overload keeps bind_data's 0.90 default.
    bool has_confidence_arg = args.ColumnCount() > 7;

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Unified formats for all inputs
    UnifiedVectorFormat date_list_data, value_list_data, members_list_data;
    UnifiedVectorFormat horizon_data, freq_data, method_data, period_data;
    UnifiedVectorFormat confidence_data;
    date_list_vec.ToUnifiedFormat(count, date_list_data);
    value_list_vec.ToUnifiedFormat(count, value_list_data);
    members_list_vec.ToUnifiedFormat(count, members_list_data);
    horizon_vec.ToUnifiedFormat(count, horizon_data);
    freq_vec.ToUnifiedFormat(count, freq_data);
    method_vec.ToUnifiedFormat(count, method_data);
    period_vec.ToUnifiedFormat(count, period_data);
    if (has_confidence_arg) {
        args.data[7].ToUnifiedFormat(count, confidence_data);
    }

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto date_idx  = date_list_data.sel->get_index(row_idx);
        auto value_idx = value_list_data.sel->get_index(row_idx);
        auto member_idx = members_list_data.sel->get_index(row_idx);

        if (!date_list_data.validity.RowIsValid(date_idx) ||
            !value_list_data.validity.RowIsValid(value_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Extract members LIST(VARCHAR) ---
        vector<string> member_names;
        if (!members_list_data.validity.RowIsValid(member_idx)) {
            throw InvalidInputException(
                "ts_forecast_ensemble_by: 'members' must not be NULL. "
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
                "ts_forecast_ensemble_by: at least 2 members are required. Got %zu.",
                member_names.size());
        }

        // --- Extract values from LIST(DOUBLE) ---
        auto value_entries = UnifiedVectorFormat::GetData<list_entry_t>(value_list_data);
        auto &value_entry  = value_entries[value_idx];
        auto &value_child  = ListVector::GetEntry(value_list_vec);

        UnifiedVectorFormat value_child_data;
        value_child.ToUnifiedFormat(ListVector::GetListSize(value_list_vec), value_child_data);
        auto value_values = UnifiedVectorFormat::GetData<double>(value_child_data);

        idx_t n = value_entry.length;
        idx_t offset = value_entry.offset;

        if (n == 0) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Extract dates from LIST(date) ---
        auto date_entries = UnifiedVectorFormat::GetData<list_entry_t>(date_list_data);
        auto &date_entry  = date_entries[date_idx];
        auto &date_child  = ListVector::GetEntry(date_list_vec);

        // Build sorted index by date. date_is_null tracks which entries had a
        // NULL date (date_micros[i]=0 is a placeholder, not a real
        // 1970-01-01 observation) so the calendar `observed` series below
        // never includes a NULL date disguised as epoch zero (D-12, plan
        // 12-06).
        vector<int64_t> date_micros(n);
        vector<bool> date_is_null(n);
        for (idx_t i = 0; i < n; i++) {
            Value dv = date_child.GetValue(date_entry.offset + i);
            if (dv.IsNull()) {
                date_micros[i] = 0;
                date_is_null[i] = true;
            } else {
                date_is_null[i] = false;
                switch (bind_data.date_col_type) {
                    case DateColumnType::DATE:
                        date_micros[i] = DateToMicroseconds(dv.GetValue<date_t>());
                        break;
                    case DateColumnType::TIMESTAMP:
                        date_micros[i] = TimestampToMicroseconds(dv.GetValue<timestamp_t>());
                        break;
                    case DateColumnType::INTEGER:
                        date_micros[i] = static_cast<int64_t>(dv.GetValue<int32_t>());
                        break;
                    case DateColumnType::BIGINT:
                        date_micros[i] = dv.GetValue<int64_t>();
                        break;
                    default:
                        date_micros[i] = 0;
                        break;
                }
            }
        }

        vector<size_t> indices(n);
        for (size_t i = 0; i < n; i++) indices[i] = i;
        std::sort(indices.begin(), indices.end(),
            [&date_micros](size_t a, size_t b) { return date_micros[a] < date_micros[b]; });

        // Build sorted values + validity, and the NON-NULL-date `observed`
        // series (D-12) in the same ascending order — a subsequence of the
        // already-sorted `indices`, so it stays ascending without a second sort.
        vector<double> sorted_values(n);
        size_t validity_words = (n + 63) / 64;
        vector<uint64_t> validity_bits(validity_words, 0);
        vector<int64_t> observed;
        observed.reserve(n);

        for (size_t i = 0; i < n; i++) {
            idx_t src       = indices[i];
            idx_t child_idx = offset + src;
            auto unified_idx = value_child_data.sel->get_index(child_idx);

            if (value_child_data.validity.RowIsValid(unified_idx)) {
                sorted_values[i] = value_values[unified_idx];
                validity_bits[i / 64] |= (1ULL << (i % 64));
            } else {
                sorted_values[i] = 0.0;
            }
            if (!date_is_null[src]) {
                observed.push_back(date_micros[src]);
            }
        }

        if (observed.empty()) {
            throw InvalidInputException("ts_forecast_ensemble_by: all date values are NULL for this group");
        }

        // --- Parse per-row parameters ---
        auto h_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = static_cast<int32_t>(bind_data.horizon);
        if (horizon_data.validity.RowIsValid(h_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[h_idx];
        }

        auto f_idx = freq_data.sel->get_index(row_idx);
        auto freq_parsed  = bind_data.frequency_type;
        auto freq_seconds = bind_data.frequency_seconds;
        auto freq_is_raw  = bind_data.frequency_is_raw;
        if (freq_data.validity.RowIsValid(f_idx)) {
            string freq_str = UnifiedVectorFormat::GetData<string_t>(freq_data)[f_idx].GetString();
            auto parsed = ParseFrequencyWithType(freq_str);
            freq_seconds = parsed.seconds;
            freq_is_raw  = parsed.is_raw;
            freq_parsed  = parsed.type;
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

        double confidence_level = bind_data.confidence_level;
        if (has_confidence_arg) {
            auto c_idx = confidence_data.sel->get_index(row_idx);
            if (confidence_data.validity.RowIsValid(c_idx)) {
                confidence_level = UnifiedVectorFormat::GetData<double>(confidence_data)[c_idx];
            }
        }

        // --- Build null-delimited member buffer for FFI ---
        std::string members_buf;
        for (const auto &name : member_names) {
            members_buf += name;
            members_buf += '\0';
        }
        size_t members_count = member_names.size();

        // --- Call Rust FFI ---
        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;
        memset(&error, 0, sizeof(error));

        bool success = anofox_ts_forecast_ensemble(
            sorted_values.data(),
            validity_bits.empty() ? nullptr : validity_bits.data(),
            sorted_values.size(),
            members_buf.data(),
            members_buf.size(),          // members_buf_len (total bytes including NULs)
            members_count,
            combination_method.c_str(),
            static_cast<int>(seasonal_period),
            static_cast<int>(horizon),
            confidence_level,
            &fcst_result,
            &error
        );

        if (!success) {
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }
            // Computation/data errors: emit NULL for this row (skip group)
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Build LIST(STRUCT(...)) result ---
        // Forecast dates computed via the Rust D-12 calendar rule
        // (StepFromFrequency + ComputeFutureDates, plan 12-05/12-06) instead
        // of this file's own month arithmetic + the day-of-month clamp this file used to replicate per-call.
        int64_t step_months = 0;
        int64_t step_micros = 0;
        StepFromFrequency(freq_parsed, freq_seconds, freq_is_raw, bind_data.date_col_type, step_months,
                           step_micros);
        vector<int64_t> future_ts =
            ComputeFutureDates(observed, fcst_result.n_forecasts, step_months, step_micros);

        // Helper lambda to convert micros back to DuckDB Value based on date col type
        auto micros_to_date_value = [&](int64_t micros) -> Value {
            switch (bind_data.date_col_type) {
                case DateColumnType::DATE:      return Value::DATE(MicrosecondsToDate(micros));
                case DateColumnType::TIMESTAMP: return Value::TIMESTAMP(MicrosecondsToTimestamp(micros));
                case DateColumnType::INTEGER:   return Value::INTEGER(static_cast<int32_t>(micros));
                case DateColumnType::BIGINT:    return Value::BIGINT(micros);
                default:                        return Value::BIGINT(micros);
            }
        };

        vector<Value> forecast_structs;
        forecast_structs.reserve(fcst_result.n_forecasts);

        for (size_t i = 0; i < fcst_result.n_forecasts; i++) {
            int64_t step = static_cast<int64_t>(i + 1);
            int64_t forecast_date = future_ts[i];

            child_list_t<Value> struct_values;
            struct_values.push_back(make_pair("forecast_step",
                Value::INTEGER(static_cast<int32_t>(step))));
            struct_values.push_back(make_pair("ds",
                micros_to_date_value(forecast_date)));
            struct_values.push_back(make_pair("yhat",
                Value::DOUBLE(fcst_result.point_forecasts[i])));
            // yhat_lower/yhat_upper are the crate Ensemble's own pooled
            // prediction interval bounds at confidence_level (D-01,
            // plan 10-03) — NULL only when the crate itself returns none.
            struct_values.push_back(make_pair("yhat_lower",
                (fcst_result.lower_bounds != nullptr)
                    ? Value::DOUBLE(fcst_result.lower_bounds[i])
                    : Value(LogicalType::DOUBLE)));
            struct_values.push_back(make_pair("yhat_upper",
                (fcst_result.upper_bounds != nullptr)
                    ? Value::DOUBLE(fcst_result.upper_bounds[i])
                    : Value(LogicalType::DOUBLE)));
            struct_values.push_back(make_pair("model_name",
                Value(string(fcst_result.model_name))));

            forecast_structs.push_back(Value::STRUCT(std::move(struct_values)));
        }

        anofox_free_forecast_result(&fcst_result);

        // Set the LIST value
        result.SetValue(row_idx, Value::LIST(std::move(forecast_structs)));
    }
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsForecastEnsembleNativeFunction(ExtensionLoader &loader) {
    // _ts_forecast_ensemble_native(
    //   dates  LIST(ANY),     values LIST(DOUBLE), members LIST(VARCHAR),
    //   horizon INTEGER,      frequency VARCHAR,
    //   combination_method VARCHAR, seasonal_period INTEGER
    //   [, confidence_level DOUBLE])
    // -> LIST(STRUCT(forecast_step INT, ds ANY, yhat DOUBLE,
    //                yhat_lower DOUBLE, yhat_upper DOUBLE, model_name VARCHAR))
    //
    // Two overloads (plan 10-03 D-01): the 7-argument form keeps the
    // documented confidence_level=0.90 default; the 8-argument form lets
    // ts_forecast_ensemble_by pass an explicit confidence_level.
    ScalarFunctionSet func_set("_ts_forecast_ensemble_native");

    func_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::ANY),      // dates
         LogicalType::LIST(LogicalType::DOUBLE),   // values
         LogicalType::LIST(LogicalType::VARCHAR),  // members
         LogicalType::INTEGER,                      // horizon
         LogicalType::VARCHAR,                      // frequency
         LogicalType::VARCHAR,                      // combination_method
         LogicalType::INTEGER},                     // seasonal_period
        LogicalType::LIST(LogicalType::ANY),        // return type set by bind
        TsForecastEnsembleNativeExecute,
        TsForecastEnsembleNativeBind));

    func_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::ANY),      // dates
         LogicalType::LIST(LogicalType::DOUBLE),   // values
         LogicalType::LIST(LogicalType::VARCHAR),  // members
         LogicalType::INTEGER,                      // horizon
         LogicalType::VARCHAR,                      // frequency
         LogicalType::VARCHAR,                      // combination_method
         LogicalType::INTEGER,                       // seasonal_period
         LogicalType::DOUBLE},                       // confidence_level
        LogicalType::LIST(LogicalType::ANY),        // return type set by bind
        TsForecastEnsembleNativeExecute,
        TsForecastEnsembleNativeBind));

    for (auto &func : func_set.functions) {
        func.null_handling = FunctionNullHandling::SPECIAL_HANDLING;
    }

    CreateScalarFunctionInfo info(func_set);
    loader.RegisterFunction(info);
}

} // namespace duckdb
