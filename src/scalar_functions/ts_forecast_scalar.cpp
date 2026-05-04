#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_fill_gaps_native.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/vector.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace duckdb {

// ============================================================================
// _ts_forecast_scalar — scalar function for parallel GROUP BY forecasting
//
// Signature:
//   _ts_forecast_scalar(dates LIST, values LIST, horizon INT, frequency VARCHAR,
//                       method VARCHAR, params MAP) -> LIST(STRUCT(...))
//
// Called per-group by the ts_forecast_by macro via:
//   SELECT group_col, unnest(_ts_forecast_scalar(...), recursive := true)
//   FROM source GROUP BY group_col
//
// DuckDB parallelizes the GROUP BY across cores natively.
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsForecastScalarBindData : public FunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;

    string method = "AutoETS";
    string model_spec = "";
    int64_t seasonal_period = 0;
    double confidence_level = 0.90;
    int64_t window = 0;
    string seasonal_periods_str = "";
    string model_pool = "";

    DateColumnType date_col_type = DateColumnType::DATE;

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<TsForecastScalarBindData>();
        copy->horizon = horizon;
        copy->frequency_seconds = frequency_seconds;
        copy->frequency_is_raw = frequency_is_raw;
        copy->frequency_type = frequency_type;
        copy->method = method;
        copy->model_spec = model_spec;
        copy->seasonal_period = seasonal_period;
        copy->confidence_level = confidence_level;
        copy->window = window;
        copy->seasonal_periods_str = seasonal_periods_str;
        copy->model_pool = model_pool;
        copy->date_col_type = date_col_type;
        return std::move(copy);
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsForecastScalarBindData>();
        return horizon == other.horizon && frequency_seconds == other.frequency_seconds &&
               method == other.method && seasonal_period == other.seasonal_period &&
               confidence_level == other.confidence_level;
    }
};

// ============================================================================
// Parameter Parsing Helpers (shared with ts_forecast_native.cpp)
// ============================================================================

static string ParseStringParam(const Value &params_value, const string &key, const string &default_val) {
    if (params_value.IsNull()) return default_val;
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) return v.ToString();
        }
    }
    return default_val;
}

static int64_t ParseInt64Param(const Value &params_value, const string &key, int64_t default_val) {
    string str = ParseStringParam(params_value, key, "");
    if (str.empty()) return default_val;
    try { return std::stoll(str); } catch (...) { return default_val; }
}

static double ParseDoubleParam(const Value &params_value, const string &key, double default_val) {
    string str = ParseStringParam(params_value, key, "");
    if (str.empty()) return default_val;
    try { return std::stod(str); } catch (...) { return default_val; }
}

static void ValidateParams(const Value &params_value, const string &method) {
    static const unordered_set<string> valid_keys = {
        "model", "seasonal_period", "seasonal_periods", "confidence_level", "window", "model_pool"
    };

    if (params_value.IsNull()) return;

    vector<string> unknown_keys;
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &key = StructValue::GetChildren(child)[0];
            string key_str = key.ToString();
            if (valid_keys.find(key_str) == valid_keys.end()) {
                unknown_keys.push_back(key_str);
            }
        }
    }

    if (!unknown_keys.empty()) {
        string unknown_list;
        for (size_t i = 0; i < unknown_keys.size(); i++) {
            if (i > 0) unknown_list += ", ";
            unknown_list += "'" + unknown_keys[i] + "'";
        }
        throw InvalidInputException(
            "Unknown parameter(s): %s. Valid parameters are: model, seasonal_period, seasonal_periods, confidence_level, window, model_pool",
            unknown_list);
    }
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsForecastScalarBind(
    ClientContext &context,
    ScalarFunction &bound_function,
    vector<unique_ptr<Expression>> &arguments) {

    auto bind_data = make_uniq<TsForecastScalarBindData>();

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
                throw InvalidInputException(
                    "Date list must contain DATE, TIMESTAMP, INTEGER, or BIGINT, got: %s",
                    child_type.ToString().c_str());
        }
    }

    // Build the output STRUCT type for list elements
    // Output: LIST(STRUCT(forecast_step INT, ds <date_type>, yhat DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE, model_name VARCHAR))
    auto &date_child_type = ListType::GetChildType(date_list_type);
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
// Date Conversion Helpers
// ============================================================================

static int64_t ExtractDateMicros(const Value &date_val, DateColumnType date_col_type) {
    switch (date_col_type) {
        case DateColumnType::DATE:
            return DateToMicroseconds(date_val.GetValue<date_t>());
        case DateColumnType::TIMESTAMP:
            return TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
        case DateColumnType::INTEGER:
            return static_cast<int64_t>(date_val.GetValue<int32_t>());
        case DateColumnType::BIGINT:
            return date_val.GetValue<int64_t>();
        default:
            return 0;
    }
}

static Value MicrosToDateValue(int64_t micros, DateColumnType date_col_type) {
    switch (date_col_type) {
        case DateColumnType::DATE:
            return Value::DATE(MicrosecondsToDate(micros));
        case DateColumnType::TIMESTAMP:
            return Value::TIMESTAMP(MicrosecondsToTimestamp(micros));
        case DateColumnType::INTEGER:
            return Value::INTEGER(static_cast<int32_t>(micros));
        case DateColumnType::BIGINT:
            return Value::BIGINT(micros);
        default:
            return Value::BIGINT(micros);
    }
}

// ============================================================================
// Compute Forecast Date (calendar-aware)
// ============================================================================

static int64_t ComputeForecastDate(int64_t last_date, int64_t step,
                                    const TsForecastScalarBindData &bind_data) {
    if (bind_data.frequency_type == FrequencyType::MONTHLY ||
        bind_data.frequency_type == FrequencyType::QUARTERLY ||
        bind_data.frequency_type == FrequencyType::YEARLY) {
        date_t base_date = MicrosecondsToDate(last_date);
        int32_t year, month, day;
        Date::Convert(base_date, year, month, day);

        int64_t months_to_add = step * bind_data.frequency_seconds;
        if (bind_data.frequency_type == FrequencyType::QUARTERLY) {
            months_to_add *= 3;
        } else if (bind_data.frequency_type == FrequencyType::YEARLY) {
            months_to_add *= 12;
        }

        int64_t total_months = static_cast<int64_t>(year) * 12 + (month - 1) + months_to_add;
        int32_t new_year = static_cast<int32_t>(total_months / 12);
        int32_t new_month = static_cast<int32_t>((total_months % 12) + 1);

        if (new_month < 1) {
            new_month += 12;
            new_year -= 1;
        }

        int32_t max_day = Date::MonthDays(new_year, new_month);
        int32_t new_day = std::min(day, max_day);

        date_t new_date = Date::FromDate(new_year, new_month, new_day);
        return DateToMicroseconds(new_date);
    } else {
        int64_t freq_micros;
        if (bind_data.date_col_type == DateColumnType::INTEGER ||
            bind_data.date_col_type == DateColumnType::BIGINT) {
            freq_micros = bind_data.frequency_seconds;
        } else {
            freq_micros = bind_data.frequency_is_raw
                ? bind_data.frequency_seconds * 86400LL * 1000000LL
                : bind_data.frequency_seconds * 1000000LL;
        }
        return last_date + freq_micros * step;
    }
}

// ============================================================================
// Execute Function
// ============================================================================

static void TsForecastScalarExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &bind_data = state.expr.Cast<BoundFunctionExpression>().bind_info->Cast<TsForecastScalarBindData>();
    idx_t count = args.size();

    auto &date_list_vec = args.data[0];   // LIST(date)
    auto &value_list_vec = args.data[1];  // LIST(DOUBLE)
    auto &horizon_vec = args.data[2];     // INTEGER
    auto &freq_vec = args.data[3];        // VARCHAR
    auto &method_vec = args.data[4];      // VARCHAR
    auto &params_vec = args.data[5];      // MAP

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Unified formats for all inputs
    UnifiedVectorFormat date_list_data, value_list_data, horizon_data, freq_data, method_data, params_data;
    date_list_vec.ToUnifiedFormat(count, date_list_data);
    value_list_vec.ToUnifiedFormat(count, value_list_data);
    horizon_vec.ToUnifiedFormat(count, horizon_data);
    freq_vec.ToUnifiedFormat(count, freq_data);
    method_vec.ToUnifiedFormat(count, method_data);
    params_vec.ToUnifiedFormat(count, params_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto date_idx = date_list_data.sel->get_index(row_idx);
        auto value_idx = value_list_data.sel->get_index(row_idx);

        if (!date_list_data.validity.RowIsValid(date_idx) ||
            !value_list_data.validity.RowIsValid(value_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Extract values from LIST(DOUBLE) ---
        auto value_entries = UnifiedVectorFormat::GetData<list_entry_t>(value_list_data);
        auto &value_entry = value_entries[value_idx];
        auto &value_child = ListVector::GetEntry(value_list_vec);

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
        auto &date_entry = date_entries[date_idx];
        auto &date_child = ListVector::GetEntry(date_list_vec);

        // Build sorted index by date
        vector<int64_t> date_micros(n);
        for (idx_t i = 0; i < n; i++) {
            Value dv = date_child.GetValue(date_entry.offset + i);
            if (dv.IsNull()) {
                date_micros[i] = 0;
            } else {
                date_micros[i] = ExtractDateMicros(dv, bind_data.date_col_type);
            }
        }

        vector<size_t> indices(n);
        for (size_t i = 0; i < n; i++) indices[i] = i;
        std::sort(indices.begin(), indices.end(),
            [&date_micros](size_t a, size_t b) { return date_micros[a] < date_micros[b]; });

        // Build sorted values + validity
        vector<double> sorted_values(n);
        size_t validity_words = (n + 63) / 64;
        vector<uint64_t> validity(validity_words, 0);
        int64_t last_date = 0;

        for (size_t i = 0; i < n; i++) {
            idx_t src = indices[i];
            idx_t child_idx = offset + src;
            auto unified_idx = value_child_data.sel->get_index(child_idx);

            if (value_child_data.validity.RowIsValid(unified_idx)) {
                sorted_values[i] = value_values[unified_idx];
                validity[i / 64] |= (1ULL << (i % 64));
            } else {
                sorted_values[i] = 0.0;
            }
            last_date = date_micros[indices[i]];
        }

        // --- Parse per-row parameters ---
        auto h_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = bind_data.horizon;
        if (horizon_data.validity.RowIsValid(h_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[h_idx];
        }

        auto m_idx = method_data.sel->get_index(row_idx);
        string method = bind_data.method;
        if (method_data.validity.RowIsValid(m_idx)) {
            method = UnifiedVectorFormat::GetData<string_t>(method_data)[m_idx].GetString();
        }

        auto f_idx = freq_data.sel->get_index(row_idx);
        auto freq_parsed = bind_data.frequency_type;
        auto freq_seconds = bind_data.frequency_seconds;
        auto freq_is_raw = bind_data.frequency_is_raw;
        if (freq_data.validity.RowIsValid(f_idx)) {
            string freq_str = UnifiedVectorFormat::GetData<string_t>(freq_data)[f_idx].GetString();
            auto parsed = ParseFrequencyWithType(freq_str);
            freq_seconds = parsed.seconds;
            freq_is_raw = parsed.is_raw;
            freq_parsed = parsed.type;
        }

        // Parse MAP params
        int64_t seasonal_period = bind_data.seasonal_period;
        double confidence_level = bind_data.confidence_level;
        int64_t window_param = bind_data.window;
        string model_spec = bind_data.model_spec;
        string seasonal_periods_str = bind_data.seasonal_periods_str;
        string model_pool = bind_data.model_pool;

        auto p_idx = params_data.sel->get_index(row_idx);
        if (params_data.validity.RowIsValid(p_idx)) {
            Value params_val = params_vec.GetValue(row_idx);
            ValidateParams(params_val, method);
            model_spec = ParseStringParam(params_val, "model", "");
            seasonal_period = ParseInt64Param(params_val, "seasonal_period", 0);
            confidence_level = ParseDoubleParam(params_val, "confidence_level", 0.90);
            window_param = ParseInt64Param(params_val, "window", 0);
            seasonal_periods_str = ParseStringParam(params_val, "seasonal_periods", "");
            model_pool = ParseStringParam(params_val, "model_pool", "");
        }

        // --- Build ForecastOptions ---
        ForecastOptions opts;
        memset(&opts, 0, sizeof(opts));
        strncpy(opts.model, method.c_str(), sizeof(opts.model) - 1);
        opts.model[sizeof(opts.model) - 1] = '\0';
        if (!model_spec.empty()) {
            strncpy(opts.ets_model, model_spec.c_str(), sizeof(opts.ets_model) - 1);
            opts.ets_model[sizeof(opts.ets_model) - 1] = '\0';
        }
        opts.horizon = static_cast<int>(horizon);
        opts.confidence_level = confidence_level;
        opts.seasonal_period = static_cast<int>(seasonal_period);
        opts.auto_detect_seasonality = (seasonal_period == 0 && seasonal_periods_str.empty());
        opts.include_fitted = false;
        opts.include_residuals = false;
        opts.window = static_cast<int>(window_param);
        if (!seasonal_periods_str.empty()) {
            strncpy(opts.seasonal_periods_str, seasonal_periods_str.c_str(),
                    sizeof(opts.seasonal_periods_str) - 1);
            opts.seasonal_periods_str[sizeof(opts.seasonal_periods_str) - 1] = '\0';
        }
        if (!model_pool.empty()) {
            strncpy(opts.model_pool, model_pool.c_str(), sizeof(opts.model_pool) - 1);
            opts.model_pool[sizeof(opts.model_pool) - 1] = '\0';
        }

        // --- Call Rust FFI ---
        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;

        bool success = anofox_ts_forecast(
            sorted_values.data(),
            validity.empty() ? nullptr : validity.data(),
            sorted_values.size(),
            &opts,
            &fcst_result,
            &error
        );

        if (!success) {
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Build LIST(STRUCT(...)) result ---
        // Use a temporary bind_data copy with the per-row frequency for date computation
        TsForecastScalarBindData row_bind;
        row_bind.frequency_seconds = freq_seconds;
        row_bind.frequency_is_raw = freq_is_raw;
        row_bind.frequency_type = freq_parsed;
        row_bind.date_col_type = bind_data.date_col_type;

        vector<Value> forecast_structs;
        forecast_structs.reserve(fcst_result.n_forecasts);

        for (size_t i = 0; i < fcst_result.n_forecasts; i++) {
            int64_t step = static_cast<int64_t>(i + 1);
            int64_t forecast_date = ComputeForecastDate(last_date, step, row_bind);

            child_list_t<Value> struct_values;
            struct_values.push_back(make_pair("forecast_step", Value::INTEGER(static_cast<int32_t>(step))));
            struct_values.push_back(make_pair("ds", MicrosToDateValue(forecast_date, bind_data.date_col_type)));
            struct_values.push_back(make_pair("yhat", Value::DOUBLE(fcst_result.point_forecasts[i])));
            struct_values.push_back(make_pair("yhat_lower", Value::DOUBLE(fcst_result.lower_bounds[i])));
            struct_values.push_back(make_pair("yhat_upper", Value::DOUBLE(fcst_result.upper_bounds[i])));
            struct_values.push_back(make_pair("model_name", Value(string(fcst_result.model_name))));

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

void RegisterTsForecastScalarFunction(ExtensionLoader &loader) {
    // _ts_forecast_scalar(dates LIST, values LIST(DOUBLE), horizon INT,
    //                     frequency VARCHAR, method VARCHAR, params MAP)
    // -> LIST(STRUCT(forecast_step INT, ds ANY, yhat DOUBLE, ...))
    ScalarFunction func("_ts_forecast_scalar",
        {LogicalType::LIST(LogicalType::ANY),     // dates
         LogicalType::LIST(LogicalType::DOUBLE),  // values
         LogicalType::INTEGER,                     // horizon
         LogicalType::VARCHAR,                     // frequency
         LogicalType::VARCHAR,                     // method
         LogicalType::ANY},                        // params (MAP)
        LogicalType::LIST(LogicalType::ANY),       // return type set by bind
        TsForecastScalarExecute,
        TsForecastScalarBind);

    func.null_handling = FunctionNullHandling::SPECIAL_HANDLING;

    loader.RegisterFunction(func);
}

} // namespace duckdb
