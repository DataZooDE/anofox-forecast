#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

// ============================================================================
// ts_classify_seasonality_agg - Aggregate function for seasonality classification
// Collects (timestamp, value) pairs and classifies after aggregation
// ============================================================================

// Internal state class
struct TsClassifySeasonalityAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    double period;
    bool initialized;

    TsClassifySeasonalityAggStateData() : period(0), initialized(false) {}
};

// State wrapper (trivially constructible)
struct TsClassifySeasonalityAggState {
    TsClassifySeasonalityAggStateData *data;
};

// Result type (same as scalar function)
static LogicalType GetClassifySeasonalityAggResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("timing_classification", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("modulation_type", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("has_stable_timing", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("timing_variability", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("seasonal_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("is_seasonal", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("cycle_strengths", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("weak_seasons", LogicalType::LIST(LogicalType(LogicalTypeId::BIGINT))));
    return LogicalType::STRUCT(std::move(children));
}

struct TsClassifySeasonalityAggOperation {
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
            target.data = new TsClassifySeasonalityAggStateData();
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

static void TsClassifySeasonalityAggUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                           Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];
    auto &period_vec = inputs[2];

    UnifiedVectorFormat ts_data, val_data, period_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);
    period_vec.ToUnifiedFormat(count, period_data);

    auto states = FlatVector::GetData<TsClassifySeasonalityAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);
        auto period_idx = period_data.sel->get_index(i);

        if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsClassifySeasonalityAggStateData();
        }

        if (!state.data->initialized) {
            state.data->initialized = true;
            // Get period from first valid row
            if (period_data.validity.RowIsValid(period_idx)) {
                state.data->period = UnifiedVectorFormat::GetData<double>(period_data)[period_idx];
            }
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
    }
}

static void TsClassifySeasonalityAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                             Vector &result, idx_t count, idx_t offset) {
    auto states = FlatVector::GetData<TsClassifySeasonalityAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];
        idx_t row = i + offset;

        if (!state.data || !state.data->initialized || state.data->values.empty()) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        auto &data = *state.data;
        double period = data.period;

        if (period <= 0 || data.values.size() < static_cast<size_t>(2 * period)) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

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

        // Call FFI for seasonality classification
        SeasonalityClassificationFFI class_result;
        memset(&class_result, 0, sizeof(class_result));
        AnofoxError error;

        bool success = anofox_ts_classify_seasonality(
            sorted_values.data(),
            sorted_values.size(),
            period,
            0.3,  // strength_threshold
            0.1,  // timing_threshold
            &class_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        // Call FFI for amplitude modulation detection
        AmplitudeModulationResultFFI mod_result;
        memset(&mod_result, 0, sizeof(mod_result));

        bool mod_success = anofox_ts_detect_amplitude_modulation(
            sorted_values.data(),
            sorted_values.size(),
            period,
            0.2,  // modulation_threshold
            0.3,  // seasonality_threshold
            &mod_result,
            &error
        );

        // Get struct children
        auto &children = StructVector::GetEntries(result);

        // timing_classification (index 0)
        FlatVector::GetData<string_t>(*children[0])[row] =
            StringVector::AddString(*children[0], class_result.classification);

        // modulation_type (index 1)
        if (mod_success) {
            FlatVector::GetData<string_t>(*children[1])[row] =
                StringVector::AddString(*children[1], mod_result.modulation_type);
        } else {
            FlatVector::GetData<string_t>(*children[1])[row] =
                StringVector::AddString(*children[1], "unknown");
        }

        // has_stable_timing (index 2)
        FlatVector::GetData<bool>(*children[2])[row] = class_result.has_stable_timing;

        // timing_variability (index 3)
        FlatVector::GetData<double>(*children[3])[row] = class_result.timing_variability;

        // seasonal_strength (index 4)
        FlatVector::GetData<double>(*children[4])[row] = class_result.seasonal_strength;

        // is_seasonal (index 5)
        FlatVector::GetData<bool>(*children[5])[row] = class_result.is_seasonal;

        // cycle_strengths (index 6)
        {
            auto &list_vec = *children[6];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row].offset = current_size;
            list_data[row].length = class_result.n_cycle_strengths;

            ListVector::Reserve(list_vec, current_size + class_result.n_cycle_strengths);
            ListVector::SetListSize(list_vec, current_size + class_result.n_cycle_strengths);

            auto child_data = FlatVector::GetData<double>(list_child);
            for (size_t j = 0; j < class_result.n_cycle_strengths; j++) {
                child_data[current_size + j] = class_result.cycle_strengths[j];
            }
        }

        // weak_seasons (index 7)
        {
            auto &list_vec = *children[7];
            auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
            auto &list_child = ListVector::GetEntry(list_vec);
            auto current_size = ListVector::GetListSize(list_vec);

            list_data[row].offset = current_size;
            list_data[row].length = class_result.n_weak_seasons;

            ListVector::Reserve(list_vec, current_size + class_result.n_weak_seasons);
            ListVector::SetListSize(list_vec, current_size + class_result.n_weak_seasons);

            auto child_data = FlatVector::GetData<int64_t>(list_child);
            for (size_t j = 0; j < class_result.n_weak_seasons; j++) {
                child_data[current_size + j] = static_cast<int64_t>(class_result.weak_seasons[j]);
            }
        }

        // Free FFI results
        anofox_free_seasonality_classification_result(&class_result);
        if (mod_success) {
            anofox_free_amplitude_modulation_result(&mod_result);
        }
    }
}

static void TsClassifySeasonalityAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                                            idx_t count) {
    auto src_states = FlatVector::GetData<TsClassifySeasonalityAggState *>(state_vector);
    auto tgt_states = FlatVector::GetData<TsClassifySeasonalityAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsClassifySeasonalityAggStateData();
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

static void TsClassifySeasonalityAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatVector::GetData<TsClassifySeasonalityAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsClassifySeasonalityAggFunction(ExtensionLoader &loader) {
    // ts_classify_seasonality_agg(ts_column, value_column, period) -> STRUCT
    AggregateFunction agg_func(
        "ts_classify_seasonality_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::DOUBLE)},
        GetClassifySeasonalityAggResultType(),
        AggregateFunction::StateSize<TsClassifySeasonalityAggState>,
        AggregateFunction::StateInitialize<TsClassifySeasonalityAggState, TsClassifySeasonalityAggOperation>,
        TsClassifySeasonalityAggUpdate,
        TsClassifySeasonalityAggCombine,
        TsClassifySeasonalityAggFinalize,
        nullptr,  // simple_update
        nullptr,  // bind
        TsClassifySeasonalityAggDestructor
    );

    AggregateFunctionSet agg_set("ts_classify_seasonality_agg");
    agg_set.AddFunction(agg_func);
    loader.RegisterFunction(agg_set);

    // Also register with anofox_ prefix
    AggregateFunctionSet anofox_agg_set("anofox_fcst_ts_classify_seasonality_agg");
    anofox_agg_set.AddFunction(agg_func);
    loader.RegisterFunction(anofox_agg_set);
}

} // namespace duckdb
