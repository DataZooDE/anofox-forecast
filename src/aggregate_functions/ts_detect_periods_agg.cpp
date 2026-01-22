#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

// ============================================================================
// ts_detect_periods_agg - Aggregate function for period detection
// Collects (timestamp, value) pairs and detects periods after aggregation
// ============================================================================

// Internal state class
struct TsDetectPeriodsAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    string method;
    bool initialized;

    TsDetectPeriodsAggStateData() : method("fft"), initialized(false) {}
};

// State wrapper (trivially constructible)
struct TsDetectPeriodsAggState {
    TsDetectPeriodsAggStateData *data;
};

// Result type - same as scalar function
static LogicalType GetDetectPeriodsAggResultType() {
    // Inner struct for each detected period
    child_list_t<LogicalType> period_children;
    period_children.push_back(make_pair("period", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("confidence", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("strength", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("amplitude", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("phase", LogicalType(LogicalTypeId::DOUBLE)));
    period_children.push_back(make_pair("iteration", LogicalType(LogicalTypeId::BIGINT)));
    auto period_type = LogicalType::STRUCT(std::move(period_children));

    // Outer result struct
    child_list_t<LogicalType> children;
    children.push_back(make_pair("periods", LogicalType::LIST(period_type)));
    children.push_back(make_pair("n_periods", LogicalType(LogicalTypeId::BIGINT)));
    children.push_back(make_pair("primary_period", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("method", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

struct TsDetectPeriodsAggOperation {
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
            target.data = new TsDetectPeriodsAggStateData();
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
        finalize_data.ReturnNull();
    }

    static bool IgnoreNull() {
        return true;
    }
};

// Update function for 2-argument version (timestamp, value)
static void TsDetectPeriodsAggUpdate2(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                      Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];

    UnifiedVectorFormat ts_data, val_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);

    auto states = FlatVector::GetData<TsDetectPeriodsAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);

        if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsDetectPeriodsAggStateData();
        }

        if (!state.data->initialized) {
            state.data->initialized = true;
            state.data->method = "fft";  // default method
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
    }
}

// Update function for 3-argument version (timestamp, value, method)
static void TsDetectPeriodsAggUpdate3(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                      Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];
    auto &method_vec = inputs[2];

    UnifiedVectorFormat ts_data, val_data, method_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);
    method_vec.ToUnifiedFormat(count, method_data);

    auto states = FlatVector::GetData<TsDetectPeriodsAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);
        auto method_idx = method_data.sel->get_index(i);

        if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsDetectPeriodsAggStateData();
        }

        if (!state.data->initialized) {
            state.data->initialized = true;
            // Get method from first valid row
            if (method_data.validity.RowIsValid(method_idx)) {
                auto method_str = UnifiedVectorFormat::GetData<string_t>(method_data)[method_idx];
                state.data->method = method_str.GetString();
            } else {
                state.data->method = "fft";
            }
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
    }
}

static void TsDetectPeriodsAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                       Vector &result, idx_t count, idx_t offset) {
    auto states = FlatVector::GetData<TsDetectPeriodsAggState *>(state_vector);

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
        for (const auto &p : sorted_pairs) {
            sorted_values.push_back(p.second);
        }

        // Call FFI for period detection
        FlatMultiPeriodResult period_result;
        memset(&period_result, 0, sizeof(period_result));
        AnofoxError error;

        bool success = anofox_ts_detect_periods_flat(
            sorted_values.data(),
            sorted_values.size(),
            data.method.c_str(),
            0,  // default max_period (use Rust default of 365)
            &period_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        // Get struct children
        auto &children = StructVector::GetEntries(result);

        // periods (index 0) - LIST of STRUCT
        {
            auto &list_vec = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row].offset = current_size;
            list_data[row].length = period_result.n_periods;

            if (period_result.n_periods > 0) {
                ListVector::Reserve(list_vec, current_size + period_result.n_periods);
                ListVector::SetListSize(list_vec, current_size + period_result.n_periods);

                auto &list_child = ListVector::GetEntry(list_vec);
                auto &struct_entries = StructVector::GetEntries(list_child);

                auto period_data = FlatVector::GetData<double>(*struct_entries[0]);
                auto confidence_data = FlatVector::GetData<double>(*struct_entries[1]);
                auto strength_data = FlatVector::GetData<double>(*struct_entries[2]);
                auto amplitude_data = FlatVector::GetData<double>(*struct_entries[3]);
                auto phase_data = FlatVector::GetData<double>(*struct_entries[4]);
                auto iteration_data = FlatVector::GetData<int64_t>(*struct_entries[5]);

                for (size_t j = 0; j < period_result.n_periods; j++) {
                    period_data[current_size + j] = period_result.period_values[j];
                    confidence_data[current_size + j] = period_result.confidence_values[j];
                    strength_data[current_size + j] = period_result.strength_values[j];
                    amplitude_data[current_size + j] = period_result.amplitude_values[j];
                    phase_data[current_size + j] = period_result.phase_values[j];
                    iteration_data[current_size + j] = period_result.iteration_values[j];
                }
            }
        }

        // n_periods (index 1)
        FlatVector::GetData<int64_t>(*children[1])[row] = period_result.n_periods;

        // primary_period (index 2)
        FlatVector::GetData<double>(*children[2])[row] = period_result.primary_period;

        // method (index 3)
        FlatVector::GetData<string_t>(*children[3])[row] =
            StringVector::AddString(*children[3], period_result.method);

        // Free FFI result
        anofox_free_flat_multi_period_result(&period_result);
    }
}

static void TsDetectPeriodsAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                                      idx_t count) {
    auto src_states = FlatVector::GetData<TsDetectPeriodsAggState *>(state_vector);
    auto tgt_states = FlatVector::GetData<TsDetectPeriodsAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsDetectPeriodsAggStateData();
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

static void TsDetectPeriodsAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatVector::GetData<TsDetectPeriodsAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsDetectPeriodsAggFunction(ExtensionLoader &loader) {
    auto result_type = GetDetectPeriodsAggResultType();

    // ts_detect_periods_agg(ts_column, value_column) -> STRUCT (default method)
    AggregateFunction agg_func_2(
        "ts_detect_periods_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)},
        result_type,
        AggregateFunction::StateSize<TsDetectPeriodsAggState>,
        AggregateFunction::StateInitialize<TsDetectPeriodsAggState, TsDetectPeriodsAggOperation>,
        TsDetectPeriodsAggUpdate2,
        TsDetectPeriodsAggCombine,
        TsDetectPeriodsAggFinalize,
        nullptr,  // simple_update
        nullptr,  // bind
        TsDetectPeriodsAggDestructor
    );

    // ts_detect_periods_agg(ts_column, value_column, method) -> STRUCT
    AggregateFunction agg_func_3(
        "ts_detect_periods_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::VARCHAR)},
        result_type,
        AggregateFunction::StateSize<TsDetectPeriodsAggState>,
        AggregateFunction::StateInitialize<TsDetectPeriodsAggState, TsDetectPeriodsAggOperation>,
        TsDetectPeriodsAggUpdate3,
        TsDetectPeriodsAggCombine,
        TsDetectPeriodsAggFinalize,
        nullptr,  // simple_update
        nullptr,  // bind
        TsDetectPeriodsAggDestructor
    );

    AggregateFunctionSet agg_set("ts_detect_periods_agg");
    agg_set.AddFunction(agg_func_2);
    agg_set.AddFunction(agg_func_3);
    loader.RegisterFunction(agg_set);

    // Also register with anofox_ prefix
    AggregateFunctionSet anofox_agg_set("anofox_fcst_ts_detect_periods_agg");
    anofox_agg_set.AddFunction(agg_func_2);
    anofox_agg_set.AddFunction(agg_func_3);
    loader.RegisterFunction(anofox_agg_set);
}

} // namespace duckdb
