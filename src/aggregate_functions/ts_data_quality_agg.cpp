#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

// Internal state class (allocated on heap)
struct TsDataQualityAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    bool initialized;

    TsDataQualityAggStateData() : initialized(false) {}
};

// Trivially constructible state wrapper (just a pointer)
struct TsDataQualityAggState {
    TsDataQualityAggStateData *data;
};

// Define the output STRUCT type for ts_data_quality_agg (8 metrics)
static LogicalType GetTsDataQualityAggResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("structural_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("temporal_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("magnitude_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("behavioral_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("overall_score", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("n_gaps", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_missing", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("is_constant", LogicalType(LogicalTypeId::BOOLEAN)));
    return LogicalType::STRUCT(std::move(children));
}

struct TsDataQualityAggOperation {
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
            target.data = new TsDataQualityAggStateData();
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

static void TsDataQualityAggUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                   Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];

    UnifiedVectorFormat ts_data, val_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);

    auto states = FlatVector::GetData<TsDataQualityAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);

        if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsDataQualityAggStateData();
        }

        if (!state.data->initialized) {
            state.data->initialized = true;
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
    }
}

// Helper to set struct field values
template <typename T>
static void SetStructField(Vector &result, idx_t field_idx, idx_t row_idx, T value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatVector::GetData<T>(*children[field_idx]);
    data[row_idx] = value;
}

static void TsDataQualityAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                     Vector &result, idx_t count, idx_t offset) {
    auto states = FlatVector::GetData<TsDataQualityAggState *>(state_vector);

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

        // Create validity bitfield (all valid)
        size_t validity_words = (sorted_values.size() + 63) / 64;
        vector<uint64_t> validity(validity_words, ~0ULL);

        // Call FFI function
        DataQualityResult dq_result;
        memset(&dq_result, 0, sizeof(dq_result));
        AnofoxError error;

        bool success = anofox_ts_data_quality(
            sorted_values.data(),
            validity.data(),
            sorted_values.size(),
            &dq_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        // Populate the 8 struct fields
        SetStructField<double>(result, 0, row, dq_result.structural_score);
        SetStructField<double>(result, 1, row, dq_result.temporal_score);
        SetStructField<double>(result, 2, row, dq_result.magnitude_score);
        SetStructField<double>(result, 3, row, dq_result.behavioral_score);
        SetStructField<double>(result, 4, row, dq_result.overall_score);
        SetStructField<uint64_t>(result, 5, row, dq_result.n_gaps);
        SetStructField<uint64_t>(result, 6, row, dq_result.n_missing);
        SetStructField<bool>(result, 7, row, dq_result.is_constant);
    }
}

static void TsDataQualityAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                                    idx_t count) {
    auto src_states = FlatVector::GetData<TsDataQualityAggState *>(state_vector);
    auto tgt_states = FlatVector::GetData<TsDataQualityAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsDataQualityAggStateData();
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

static void TsDataQualityAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatVector::GetData<TsDataQualityAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsDataQualityAggFunction(ExtensionLoader &loader) {
    // ts_data_quality_agg(ts_column, value_column)
    AggregateFunction agg_func(
        "ts_data_quality_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)},
        GetTsDataQualityAggResultType(),
        AggregateFunction::StateSize<TsDataQualityAggState>,
        AggregateFunction::StateInitialize<TsDataQualityAggState, TsDataQualityAggOperation>,
        TsDataQualityAggUpdate,
        TsDataQualityAggCombine,
        TsDataQualityAggFinalize,
        nullptr,  // simple_update
        nullptr,  // bind
        TsDataQualityAggDestructor
    );

    // Register ts_data_quality_agg
    AggregateFunctionSet ts_dq_agg_set("ts_data_quality_agg");
    ts_dq_agg_set.AddFunction(agg_func);
    loader.RegisterFunction(ts_dq_agg_set);

    // Register anofox_fcst_ts_data_quality_agg alias
    AggregateFunctionSet anofox_dq_agg_set("anofox_fcst_ts_data_quality_agg");
    anofox_dq_agg_set.AddFunction(agg_func);
    loader.RegisterFunction(anofox_dq_agg_set);
}

} // namespace duckdb
