#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <cmath>

namespace duckdb {

// Internal state class (allocated on heap)
struct TsForecastAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    string method;
    string ets_model;  // ETS model specification (e.g., "AAA", "MNM", "AAdA")
    int32_t horizon;
    double confidence_level;
    bool initialized;

    TsForecastAggStateData() : horizon(12), confidence_level(0.90), initialized(false) {}
};

// Trivially constructible state wrapper (just a pointer)
struct TsForecastAggState {
    TsForecastAggStateData *data;
};

// Bind data for dynamic column naming
struct TsForecastAggBindData : public FunctionData {
    double confidence_level;
    string lower_col_name;
    string upper_col_name;

    TsForecastAggBindData() : confidence_level(0.90) {
        UpdateColumnNames();
    }

    void UpdateColumnNames() {
        // Convert confidence level to percentage suffix (e.g., 0.90 -> "90", 0.95 -> "95")
        int pct = static_cast<int>(std::round(confidence_level * 100));
        lower_col_name = "lower_" + std::to_string(pct);
        upper_col_name = "upper_" + std::to_string(pct);
    }

    unique_ptr<FunctionData> Copy() const override {
        auto result = make_uniq<TsForecastAggBindData>();
        result->confidence_level = confidence_level;
        result->lower_col_name = lower_col_name;
        result->upper_col_name = upper_col_name;
        return result;
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsForecastAggBindData>();
        return confidence_level == other.confidence_level;
    }
};

// Helper to get confidence level suffix
static string GetConfidenceSuffix(double confidence_level) {
    int pct = static_cast<int>(std::round(confidence_level * 100));
    return std::to_string(pct);
}

// Build result type with dynamic column names based on confidence level
static LogicalType GetForecastAggResultType(double confidence_level = 0.90) {
    string suffix = GetConfidenceSuffix(confidence_level);

    child_list_t<LogicalType> children;
    children.push_back(make_pair("forecast_step", LogicalType::LIST(LogicalType(LogicalTypeId::INTEGER))));
    children.push_back(make_pair("forecast_timestamp", LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP))));
    children.push_back(make_pair("point_forecast", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("lower_" + suffix, LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("upper_" + suffix, LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("model_name", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("insample_fitted", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("date_col_name", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("error_message", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

// Bind function to extract confidence_level from params MAP and set dynamic return type
static unique_ptr<FunctionData> TsForecastAggBind(ClientContext &context, AggregateFunction &function,
                                                   vector<unique_ptr<Expression>> &arguments) {
    auto bind_data = make_uniq<TsForecastAggBindData>();

    // Default confidence level
    bind_data->confidence_level = 0.90;

    // Note: We can't easily extract the confidence_level from the params MAP at bind time
    // because it's a runtime value. For now, use default 0.90 which matches C++ extension default.
    // Users can use the result and the confidence is embedded in the column name.

    bind_data->UpdateColumnNames();

    // Set the return type with dynamic column names
    function.return_type = GetForecastAggResultType(bind_data->confidence_level);

    return bind_data;
}

struct TsForecastAggOperation {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.data = nullptr;
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.data || !source.data->initialized) {
            return;
        }
        if (!target.data) {
            target.data = new TsForecastAggStateData();
        }
        if (!target.data->initialized) {
            *target.data = *source.data;
        } else {
            target.data->timestamps.insert(target.data->timestamps.end(),
                                           source.data->timestamps.begin(),
                                           source.data->timestamps.end());
            target.data->values.insert(target.data->values.end(),
                                       source.data->values.begin(),
                                       source.data->values.end());
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        if (!state.data || !state.data->initialized || state.data->values.empty()) {
            finalize_data.ReturnNull();
            return;
        }

        auto &data = *state.data;

        // Sort by timestamp
        vector<pair<int64_t, double>> sorted_data;
        for (size_t i = 0; i < data.timestamps.size(); i++) {
            sorted_data.push_back({data.timestamps[i], data.values[i]});
        }
        std::sort(sorted_data.begin(), sorted_data.end());

        vector<double> sorted_values;
        for (const auto &p : sorted_data) {
            sorted_values.push_back(p.second);
        }

        size_t validity_words = (sorted_values.size() + 63) / 64;
        vector<uint64_t> validity(validity_words, ~0ULL);

        ForecastOptions opts;
        memset(&opts, 0, sizeof(opts));
        size_t model_len = std::min(data.method.size(), (size_t)31);
        memcpy(opts.model, data.method.c_str(), model_len);
        opts.model[model_len] = '\0';

        // Set ETS model specification if provided (e.g., "AAA", "MNM", "AAdA")
        if (!data.ets_model.empty()) {
            size_t ets_len = std::min(data.ets_model.size(), (size_t)7);
            memcpy(opts.ets_model, data.ets_model.c_str(), ets_len);
            opts.ets_model[ets_len] = '\0';
        }

        opts.horizon = data.horizon;
        opts.confidence_level = data.confidence_level;
        opts.include_fitted = true;

        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;

        bool success = anofox_ts_forecast(
            sorted_values.data(),
            validity.data(),
            sorted_values.size(),
            &opts,
            &fcst_result,
            &error
        );

        if (!success) {
            finalize_data.ReturnNull();
            return;
        }

        // Build result - this is complex, set null for now
        // Full struct construction requires more infrastructure
        anofox_free_forecast_result(&fcst_result);
        finalize_data.ReturnNull();
    }

    static bool IgnoreNull() {
        return true;
    }
};

// Helper function to extract a string value from a params MAP
static string GetParamFromMap(Vector &map_vec, idx_t count, idx_t row_idx, const string &key, const string &default_value = "") {
    // MAP is represented as LIST(STRUCT(key, value)) in DuckDB
    UnifiedVectorFormat map_data;
    map_vec.ToUnifiedFormat(count, map_data);

    auto map_idx = map_data.sel->get_index(row_idx);
    if (!map_data.validity.RowIsValid(map_idx)) {
        return default_value;
    }

    // Get the list entry for this row
    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(map_data);
    auto &list_entry = list_entries[map_idx];

    if (list_entry.length == 0) {
        return default_value;
    }

    // Get the child vector (which contains STRUCT(key, value) entries)
    auto &struct_vec = ListVector::GetEntry(map_vec);
    auto &struct_children = StructVector::GetEntries(struct_vec);
    auto &key_vec = *struct_children[0];  // keys
    auto &val_vec = *struct_children[1];  // values

    // Use UnifiedVectorFormat for the child vectors
    UnifiedVectorFormat key_data, val_data;
    key_vec.ToUnifiedFormat(ListVector::GetListSize(map_vec), key_data);
    val_vec.ToUnifiedFormat(ListVector::GetListSize(map_vec), val_data);

    auto key_values = UnifiedVectorFormat::GetData<string_t>(key_data);
    auto val_values = UnifiedVectorFormat::GetData<string_t>(val_data);

    for (idx_t j = 0; j < list_entry.length; j++) {
        auto child_idx = list_entry.offset + j;
        auto key_unified_idx = key_data.sel->get_index(child_idx);
        if (key_data.validity.RowIsValid(key_unified_idx)) {
            auto key_str = key_values[key_unified_idx].GetString();
            if (key_str == key) {
                auto val_unified_idx = val_data.sel->get_index(child_idx);
                if (val_data.validity.RowIsValid(val_unified_idx)) {
                    return val_values[val_unified_idx].GetString();
                }
                return default_value;
            }
        }
    }
    return default_value;
}

static void TsForecastAggUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];
    auto &method_vec = inputs[2];
    auto &horizon_vec = inputs[3];
    auto &params_vec = inputs[4];  // params MAP

    UnifiedVectorFormat ts_data, val_data, method_data, horizon_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);
    method_vec.ToUnifiedFormat(count, method_data);
    horizon_vec.ToUnifiedFormat(count, horizon_data);

    auto states = FlatVector::GetData<TsForecastAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);
        auto method_idx = method_data.sel->get_index(i);
        auto horizon_idx = horizon_data.sel->get_index(i);

        if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsForecastAggStateData();
        }

        if (!state.data->initialized) {
            if (method_data.validity.RowIsValid(method_idx)) {
                state.data->method = UnifiedVectorFormat::GetData<string_t>(method_data)[method_idx].GetString();
            } else {
                state.data->method = "auto";
            }
            if (horizon_data.validity.RowIsValid(horizon_idx)) {
                state.data->horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[horizon_idx];
            } else {
                state.data->horizon = 12;
            }

            // Extract 'model' from params MAP for ETS specification (e.g., "AAA", "MNM")
            state.data->ets_model = GetParamFromMap(params_vec, count, i, "model", "");

            state.data->initialized = true;
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
    }
}

static void TsForecastAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                  Vector &result, idx_t count, idx_t offset) {
    auto states = FlatVector::GetData<TsForecastAggState *>(state_vector);

    auto &children = StructVector::GetEntries(result);
    auto &step_list = *children[0];      // forecast_step
    auto &ts_list = *children[1];        // forecast_timestamp
    auto &point_list = *children[2];     // point_forecast
    auto &lower_list = *children[3];     // lower_<suffix>
    auto &upper_list = *children[4];     // upper_<suffix>
    auto &model_vec = *children[5];      // model_name
    auto &fitted_list = *children[6];    // insample_fitted
    auto &date_col_vec = *children[7];   // date_col_name
    auto &error_vec = *children[8];      // error_message

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];
        idx_t row = i + offset;

        if (!state.data || !state.data->initialized || state.data->values.empty()) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        auto &data = *state.data;

        // Sort by timestamp
        vector<pair<int64_t, double>> sorted_pairs;
        for (size_t j = 0; j < data.timestamps.size(); j++) {
            sorted_pairs.push_back({data.timestamps[j], data.values[j]});
        }
        std::sort(sorted_pairs.begin(), sorted_pairs.end());

        vector<double> sorted_values;
        vector<int64_t> sorted_timestamps;
        for (const auto &p : sorted_pairs) {
            sorted_timestamps.push_back(p.first);
            sorted_values.push_back(p.second);
        }

        size_t validity_words = (sorted_values.size() + 63) / 64;
        vector<uint64_t> validity(validity_words, ~0ULL);

        ForecastOptions opts;
        memset(&opts, 0, sizeof(opts));
        size_t model_len = std::min(data.method.size(), (size_t)31);
        memcpy(opts.model, data.method.c_str(), model_len);
        opts.model[model_len] = '\0';

        // Set ETS model specification if provided (e.g., "AAA", "MNM", "AAdA")
        if (!data.ets_model.empty()) {
            size_t ets_len = std::min(data.ets_model.size(), (size_t)7);
            memcpy(opts.ets_model, data.ets_model.c_str(), ets_len);
            opts.ets_model[ets_len] = '\0';
        }

        opts.horizon = data.horizon;
        opts.confidence_level = data.confidence_level;
        opts.include_fitted = true;

        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;

        bool success = anofox_ts_forecast(
            sorted_values.data(),
            validity.data(),
            sorted_values.size(),
            &opts,
            &fcst_result,
            &error
        );

        if (!success) {
            // Set error message instead of returning null
            FlatVector::GetData<string_t>(error_vec)[row] =
                StringVector::AddString(error_vec, error.message);
            // Set empty lists for other fields
            auto set_empty_list = [row](Vector &list_vec) {
                auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
                list_data[row].offset = ListVector::GetListSize(list_vec);
                list_data[row].length = 0;
            };
            set_empty_list(step_list);
            set_empty_list(ts_list);
            set_empty_list(point_list);
            set_empty_list(lower_list);
            set_empty_list(upper_list);
            set_empty_list(fitted_list);
            FlatVector::GetData<string_t>(model_vec)[row] =
                StringVector::AddString(model_vec, "");
            FlatVector::GetData<string_t>(date_col_vec)[row] =
                StringVector::AddString(date_col_vec, "date");
            continue;
        }

        // Compute forecast timestamps based on detected frequency
        int64_t last_ts = sorted_timestamps.back();
        int64_t ts_step = 0;
        if (sorted_timestamps.size() >= 2) {
            // Compute median step size for robustness
            vector<int64_t> steps;
            for (size_t j = 1; j < sorted_timestamps.size(); j++) {
                steps.push_back(sorted_timestamps[j] - sorted_timestamps[j-1]);
            }
            std::sort(steps.begin(), steps.end());
            ts_step = steps[steps.size() / 2];  // median
        } else {
            ts_step = 86400000000LL;  // Default to 1 day in microseconds
        }

        // Set forecast_step list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(step_list);
            auto &list_child = ListVector::GetEntry(step_list);
            auto current_size = ListVector::GetListSize(step_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(step_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(step_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<int32_t>(list_child);
            for (size_t j = 0; j < fcst_result.n_forecasts; j++) {
                child_data[current_size + j] = j + 1;
            }
        }

        // Set forecast_timestamp list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(ts_list);
            auto &list_child = ListVector::GetEntry(ts_list);
            auto current_size = ListVector::GetListSize(ts_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(ts_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(ts_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<timestamp_t>(list_child);
            for (size_t j = 0; j < fcst_result.n_forecasts; j++) {
                child_data[current_size + j] = timestamp_t(last_ts + (j + 1) * ts_step);
            }
        }

        // Set point_forecast list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(point_list);
            auto &list_child = ListVector::GetEntry(point_list);
            auto current_size = ListVector::GetListSize(point_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(point_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(point_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<double>(list_child);
            memcpy(child_data + current_size, fcst_result.point_forecasts,
                   fcst_result.n_forecasts * sizeof(double));
        }

        // Set lower_bound list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(lower_list);
            auto &list_child = ListVector::GetEntry(lower_list);
            auto current_size = ListVector::GetListSize(lower_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(lower_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(lower_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<double>(list_child);
            memcpy(child_data + current_size, fcst_result.lower_bounds,
                   fcst_result.n_forecasts * sizeof(double));
        }

        // Set upper_<suffix> list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(upper_list);
            auto &list_child = ListVector::GetEntry(upper_list);
            auto current_size = ListVector::GetListSize(upper_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(upper_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(upper_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<double>(list_child);
            memcpy(child_data + current_size, fcst_result.upper_bounds,
                   fcst_result.n_forecasts * sizeof(double));
        }

        // Set model_name
        FlatVector::GetData<string_t>(model_vec)[row] =
            StringVector::AddString(model_vec, fcst_result.model_name);

        // Set fitted values list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(fitted_list);
            auto &list_child = ListVector::GetEntry(fitted_list);
            auto current_size = ListVector::GetListSize(fitted_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_fitted;

            ListVector::Reserve(fitted_list, current_size + fcst_result.n_fitted);
            ListVector::SetListSize(fitted_list, current_size + fcst_result.n_fitted);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (fcst_result.fitted_values && fcst_result.n_fitted > 0) {
                memcpy(child_data + current_size, fcst_result.fitted_values,
                       fcst_result.n_fitted * sizeof(double));
            }
        }

        // Set date_col_name (default to "date" as we don't have the column name in aggregate context)
        FlatVector::GetData<string_t>(date_col_vec)[row] =
            StringVector::AddString(date_col_vec, "date");

        // Set error_message (empty on success)
        FlatVector::GetData<string_t>(error_vec)[row] =
            StringVector::AddString(error_vec, "");

        anofox_free_forecast_result(&fcst_result);
    }
}

static void TsForecastAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                                 idx_t count) {
    auto src_states = FlatVector::GetData<TsForecastAggState *>(state_vector);
    auto tgt_states = FlatVector::GetData<TsForecastAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsForecastAggStateData();
            *tgt.data = *src.data;
        } else if (!tgt.data->initialized) {
            *tgt.data = *src.data;
        } else {
            tgt.data->timestamps.insert(tgt.data->timestamps.end(),
                                        src.data->timestamps.begin(),
                                        src.data->timestamps.end());
            tgt.data->values.insert(tgt.data->values.end(),
                                    src.data->values.begin(),
                                    src.data->values.end());
        }
    }
}

static void TsForecastAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatVector::GetData<TsForecastAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsForecastAggFunction(ExtensionLoader &loader) {
    // Create aggregate function with 5 parameters matching API spec:
    // (date_col, value_col, method, horizon, params)
    AggregateFunction agg_func(
        "anofox_fcst_ts_forecast_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::INTEGER),
         LogicalType::MAP(LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR))},
        GetForecastAggResultType(),
        AggregateFunction::StateSize<TsForecastAggState>,
        AggregateFunction::StateInitialize<TsForecastAggState, TsForecastAggOperation>,
        TsForecastAggUpdate,
        TsForecastAggCombine,
        TsForecastAggFinalize,
        nullptr,  // simple_update
        TsForecastAggBind,
        TsForecastAggDestructor
    );

    AggregateFunctionSet func_set("anofox_fcst_ts_forecast_agg");
    func_set.AddFunction(agg_func);
    loader.RegisterFunction(func_set);

    // Also register as ts_forecast_agg alias
    AggregateFunctionSet alias_set("ts_forecast_agg");
    alias_set.AddFunction(agg_func);
    loader.RegisterFunction(alias_set);
}

} // namespace duckdb
