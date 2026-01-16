#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

// Internal state class (allocated on heap)
struct TsChangepointsAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    double hazard_lambda;
    bool include_probabilities;
    bool initialized;

    TsChangepointsAggStateData() : hazard_lambda(250.0), include_probabilities(false), initialized(false) {}
};

// Trivially constructible state wrapper (just a pointer)
struct TsChangepointsAggState {
    TsChangepointsAggStateData *data;
};

static LogicalType GetChangepointsAggResultType() {
    // Return: LIST<STRUCT(timestamp, value, is_changepoint, changepoint_probability)>
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("timestamp", LogicalType(LogicalTypeId::TIMESTAMP)));
    struct_children.push_back(make_pair("value", LogicalType(LogicalTypeId::DOUBLE)));
    struct_children.push_back(make_pair("is_changepoint", LogicalType(LogicalTypeId::BOOLEAN)));
    struct_children.push_back(make_pair("changepoint_probability", LogicalType(LogicalTypeId::DOUBLE)));

    return LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));
}

struct TsChangepointsAggOperation {
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
            target.data = new TsChangepointsAggStateData();
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
        // Not used - we use custom finalize
        finalize_data.ReturnNull();
    }

    static bool IgnoreNull() {
        return true;
    }
};

static void TsChangepointsAggUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                    Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];
    auto &params_vec = inputs[2];

    UnifiedVectorFormat ts_data, val_data, params_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);
    params_vec.ToUnifiedFormat(count, params_data);

    auto states = FlatVector::GetData<TsChangepointsAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);
        auto params_idx = params_data.sel->get_index(i);

        if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsChangepointsAggStateData();
        }

        if (!state.data->initialized) {
            // Parse params MAP if valid
            // Default values already set in constructor
            state.data->initialized = true;
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
    }
}

static void TsChangepointsAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                      Vector &result, idx_t count, idx_t offset) {
    auto states = FlatVector::GetData<TsChangepointsAggState *>(state_vector);

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

        // Call BOCPD changepoint detection
        BocpdResult cp_result;
        memset(&cp_result, 0, sizeof(cp_result));
        AnofoxError error;

        bool success = anofox_ts_detect_changepoints_bocpd(
            sorted_values.data(),
            sorted_values.size(),
            data.hazard_lambda,
            true,  // always include probabilities for aggregate
            &cp_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        // Build LIST<STRUCT(timestamp, value, is_changepoint, changepoint_probability)>
        auto list_data = FlatVector::GetData<list_entry_t>(result);
        auto &list_child = ListVector::GetEntry(result);  // This is the STRUCT vector
        auto current_size = ListVector::GetListSize(result);

        size_t n = sorted_values.size();

        list_data[row].offset = current_size;
        list_data[row].length = n;

        ListVector::Reserve(result, current_size + n);
        ListVector::SetListSize(result, current_size + n);

        // Get the struct children
        auto &struct_entries = StructVector::GetEntries(list_child);
        auto &ts_child = *struct_entries[0];
        auto &val_child = *struct_entries[1];
        auto &cp_child = *struct_entries[2];
        auto &prob_child = *struct_entries[3];

        auto ts_child_data = FlatVector::GetData<timestamp_t>(ts_child);
        auto val_child_data = FlatVector::GetData<double>(val_child);
        auto cp_child_data = FlatVector::GetData<bool>(cp_child);
        auto prob_child_data = FlatVector::GetData<double>(prob_child);

        for (size_t j = 0; j < n; j++) {
            ts_child_data[current_size + j] = timestamp_t(sorted_timestamps[j]);
            val_child_data[current_size + j] = sorted_values[j];
            cp_child_data[current_size + j] = cp_result.is_changepoint[j];
            prob_child_data[current_size + j] = cp_result.changepoint_probability ?
                cp_result.changepoint_probability[j] : 0.0;
        }

        anofox_free_bocpd_result(&cp_result);
    }
}

static void TsChangepointsAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                                     idx_t count) {
    auto src_states = FlatVector::GetData<TsChangepointsAggState *>(state_vector);
    auto tgt_states = FlatVector::GetData<TsChangepointsAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsChangepointsAggStateData();
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

static void TsChangepointsAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatVector::GetData<TsChangepointsAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsDetectChangepointsAggFunction(ExtensionLoader &loader) {
    // Create aggregate function with 3 parameters:
    // (date_col, value_col, params MAP)
    AggregateFunction agg_func(
        "ts_detect_changepoints_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE),
         LogicalType::MAP(LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR))},
        GetChangepointsAggResultType(),
        AggregateFunction::StateSize<TsChangepointsAggState>,
        AggregateFunction::StateInitialize<TsChangepointsAggState, TsChangepointsAggOperation>,
        TsChangepointsAggUpdate,
        TsChangepointsAggCombine,
        TsChangepointsAggFinalize,
        nullptr,  // simple_update
        nullptr,  // bind
        TsChangepointsAggDestructor
    );

    AggregateFunctionSet func_set("ts_detect_changepoints_agg");
    func_set.AddFunction(agg_func);
    loader.RegisterFunction(func_set);

    // Also register with anofox_fcst_ prefix
    AggregateFunctionSet alias_set("anofox_fcst_ts_detect_changepoints_agg");
    alias_set.AddFunction(agg_func);
    loader.RegisterFunction(alias_set);
}

} // namespace duckdb
