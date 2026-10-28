#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/parser/parsed_data/create_aggregate_function_info.hpp"
#include <numeric>
#include <algorithm>

namespace duckdb {

// Internal state class (allocated on heap)
struct TsStatsAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    vector<bool> validity;  // false = NULL-valued row (kept, placeholder value)
    bool initialized;

    TsStatsAggStateData() : initialized(false) {}
};

// Trivially constructible state wrapper (just a pointer)
struct TsStatsAggState {
    TsStatsAggStateData *data;
};

// Define the output STRUCT type for ts_stats_agg (34 metrics)
static LogicalType GetTsStatsAggResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("length", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_nulls", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_nan", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_zeros", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_positive", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_negative", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_unique_values", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("is_constant", LogicalType(LogicalTypeId::BOOLEAN)));
    children.push_back(make_pair("n_zeros_start", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_zeros_end", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("plateau_size", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("plateau_size_nonzero", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("mean", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("median", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("std_dev", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("variance", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("min", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("max", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("range", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("sum", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("skewness", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("kurtosis", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("tail_index", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("bimodality_coef", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("trimmed_mean", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("coef_variation", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("q1", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("q3", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("iqr", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("autocorr_lag1", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("trend_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("seasonality_strength", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("entropy", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("stability", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("expected_length", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_gaps", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("n_gap_runs", LogicalType(LogicalTypeId::UBIGINT)));
    return LogicalType::STRUCT(std::move(children));
}

struct TsStatsAggOperation {
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
            target.data = new TsStatsAggStateData();
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
            target.data->validity.insert(target.data->validity.end(),
                                         source.data->validity.begin(),
                                         source.data->validity.end());
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        // Not used - we use custom finalize
        finalize_data.ReturnNull();
    }

    static bool IgnoreNull() {
        // Must see NULL-valued rows too (kept, counted in n_nulls); only a NULL
        // timestamp is skipped, and that is handled explicitly in TsStatsAggUpdate.
        return false;
    }
};

static void TsStatsAggUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                             Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];

    UnifiedVectorFormat ts_data, val_data;
    ToUnified(ts_vec, count, ts_data);
    ToUnified(val_vec, count, val_data);

    // Window evaluation passes a constant or dictionary state vector, GROUP BY a
    // flat one: read it through UnifiedVectorFormat so both are handled.
    UnifiedVectorFormat state_data;
    ToUnified(state_vector, count, state_data);
    auto states = UnifiedVectorFormat::GetData<TsStatsAggState *>(state_data);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[state_data.sel->get_index(i)];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);

        // A NULL timestamp cannot be placed in time, so that row is skipped entirely.
        // A NULL value is KEPT (Phase 12 deferred item (a), fixed in Phase 13): it is
        // counted in n_nulls and preserves its position in the date-ordered series,
        // matching ts_stats_by's NULL-value policy.
        if (!ts_data.validity.RowIsValid(ts_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsStatsAggStateData();
        }

        if (!state.data->initialized) {
            state.data->initialized = true;
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        bool value_valid = val_data.validity.RowIsValid(val_idx);
        double val = value_valid ? UnifiedVectorFormat::GetData<double>(val_data)[val_idx] : 0.0;

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
        state.data->validity.push_back(value_valid);
    }
}

// Helper to set struct field values
template <typename T>
static void SetStructField(Vector &result, idx_t field_idx, idx_t row_idx, T value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatDataMut<T>(StructEntry(children[field_idx]));
    data[row_idx] = value;
}

static void TsStatsAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                               Vector &result, idx_t count, idx_t offset) {
    auto states = FlatDataMut<TsStatsAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];
        idx_t row = i + offset;

        if (!state.data || !state.data->initialized || state.data->values.empty()) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        auto &data = *state.data;

        // Sort by timestamp, keeping value+validity paired with it. Ties (duplicate
        // timestamps) are stable-sorted by original arrival order, same as
        // std::stable_sort leaves them (no further tie-break needed here: gap_counts
        // de-duplicates exact-timestamp ties on the Rust side regardless of order).
        size_t n = data.timestamps.size();
        vector<size_t> order(n);
        std::iota(order.begin(), order.end(), size_t(0));
        std::stable_sort(order.begin(), order.end(), [&data](size_t a, size_t b) {
            return data.timestamps[a] < data.timestamps[b];
        });

        vector<int64_t> sorted_timestamps(n);
        vector<double> sorted_values(n);
        size_t validity_words = (n + 63) / 64;
        vector<uint64_t> validity(validity_words, 0);
        for (size_t i = 0; i < n; i++) {
            size_t src = order[i];
            sorted_timestamps[i] = data.timestamps[src];
            sorted_values[i] = data.values[src];
            if (data.validity[src]) {
                validity[i / 64] |= (1ULL << (i % 64));
            }
        }

        // Call FFI function with dates, frequency_micros=0, FIXED so Rust
        // auto-detects the step (D-04 "detected frequency", Phase 13 plan 13-01).
        // seasonal_period=0 (D-02, Phase 13 plan 13-02): ts_stats_agg takes no period
        // argument, so it always auto-detects via seasonality::detect_seasonality.
        TsStatsResult stats_result;
        memset(&stats_result, 0, sizeof(stats_result));
        AnofoxError error;

        bool success = anofox_ts_stats_with_dates_and_type(
            sorted_values.data(),
            validity.data(),
            sorted_timestamps.data(),
            sorted_values.size(),
            0,
            FIXED,
            0,
            1,  // period_count: irrelevant for FIXED (CR-01 fix adds this param)
            &stats_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        // Populate the 34 struct fields
        SetStructField<uint64_t>(result, 0, row, stats_result.length);
        SetStructField<uint64_t>(result, 1, row, stats_result.n_nulls);
        SetStructField<uint64_t>(result, 2, row, stats_result.n_nan);
        SetStructField<uint64_t>(result, 3, row, stats_result.n_zeros);
        SetStructField<uint64_t>(result, 4, row, stats_result.n_positive);
        SetStructField<uint64_t>(result, 5, row, stats_result.n_negative);
        SetStructField<uint64_t>(result, 6, row, stats_result.n_unique_values);
        SetStructField<bool>(result, 7, row, stats_result.is_constant);
        SetStructField<uint64_t>(result, 8, row, stats_result.n_zeros_start);
        SetStructField<uint64_t>(result, 9, row, stats_result.n_zeros_end);
        SetStructField<uint64_t>(result, 10, row, stats_result.plateau_size);
        SetStructField<uint64_t>(result, 11, row, stats_result.plateau_size_nonzero);
        SetStructField<double>(result, 12, row, stats_result.mean);
        SetStructField<double>(result, 13, row, stats_result.median);
        SetStructField<double>(result, 14, row, stats_result.std_dev);
        SetStructField<double>(result, 15, row, stats_result.variance);
        SetStructField<double>(result, 16, row, stats_result.min);
        SetStructField<double>(result, 17, row, stats_result.max);
        SetStructField<double>(result, 18, row, stats_result.range);
        SetStructField<double>(result, 19, row, stats_result.sum);
        SetStructField<double>(result, 20, row, stats_result.skewness);
        SetStructField<double>(result, 21, row, stats_result.kurtosis);
        SetStructField<double>(result, 22, row, stats_result.tail_index);
        SetStructField<double>(result, 23, row, stats_result.bimodality_coef);
        SetStructField<double>(result, 24, row, stats_result.trimmed_mean);
        SetStructField<double>(result, 25, row, stats_result.coef_variation);
        SetStructField<double>(result, 26, row, stats_result.q1);
        SetStructField<double>(result, 27, row, stats_result.q3);
        SetStructField<double>(result, 28, row, stats_result.iqr);
        SetStructField<double>(result, 29, row, stats_result.autocorr_lag1);

        auto &children = StructVector::GetEntries(result);

        // trend_strength / seasonality_strength - NULL when the Rust side has no value
        // (D-02: interior-missing series, or -- for seasonality_strength -- no usable
        // period / zero seasonal variance).
        if (stats_result.has_trend_strength) {
            SetStructField<double>(result, 30, row, stats_result.trend_strength);
        } else {
            FlatVector::SetNull(StructEntry(children[30]), row, true);
        }
        if (stats_result.has_seasonality_strength) {
            SetStructField<double>(result, 31, row, stats_result.seasonality_strength);
        } else {
            FlatVector::SetNull(StructEntry(children[31]), row, true);
        }

        // entropy - NULL when the Rust side has no entropy value (D-03)
        if (stats_result.has_entropy) {
            SetStructField<double>(result, 32, row, stats_result.entropy);
        } else {
            FlatVector::SetNull(StructEntry(children[32]), row, true);
        }

        SetStructField<double>(result, 33, row, stats_result.stability);

        // Set date-based metrics (NULL when no frequency could be detected)
        if (stats_result.has_date_metrics) {
            SetStructField<uint64_t>(result, 34, row, stats_result.expected_length);
            SetStructField<uint64_t>(result, 35, row, stats_result.n_gaps);
            SetStructField<uint64_t>(result, 36, row, stats_result.n_gap_runs);
        } else {
            FlatVector::SetNull(StructEntry(children[34]), row, true);
            FlatVector::SetNull(StructEntry(children[35]), row, true);
            FlatVector::SetNull(StructEntry(children[36]), row, true);
        }
    }
}

static void TsStatsAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                              idx_t count) {
    auto src_states = FlatDataMut<TsStatsAggState *>(state_vector);
    auto tgt_states = FlatDataMut<TsStatsAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsStatsAggStateData();
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
            tgt.data->validity.insert(tgt.data->validity.end(),
                                      src.data->validity.begin(),
                                      src.data->validity.end());
        }
    }
}

static void TsStatsAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatDataMut<TsStatsAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsStatsAggFunction(ExtensionLoader &loader) {
    // ts_stats_agg(ts_column, value_column)
    AggregateFunction agg_func(
        "ts_stats_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)},
        GetTsStatsAggResultType(),
        AggregateFunction::StateSize<TsStatsAggState>,
        AggregateFunction::StateInitialize<TsStatsAggState, TsStatsAggOperation>,
        TsStatsAggUpdate,
        TsStatsAggCombine,
        ANOFOX_AGG_FINALIZE(TsStatsAggFinalize),
        nullptr,  // simple_update
        nullptr,  // bind
        TsStatsAggDestructor
    );

    // Register ts_stats_agg
    AggregateFunctionSet ts_stats_agg_set("ts_stats_agg");
    ts_stats_agg_set.AddFunction(agg_func);
    {
        CreateAggregateFunctionInfo info(ts_stats_agg_set);
        FunctionDescription desc;
        desc.description = "Aggregate function: computes 36 time series statistics for each group (including n_gap_runs). Returns a STRUCT with mean, std, min, max, trend, seasonality, and many more.";
        desc.examples = {"SELECT product_id, ts_stats_agg(date, value) FROM sales GROUP BY product_id"};
        desc.categories = {"time-series", "statistics"};
        desc.parameter_names = {"date", "value"};
        desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    // Register anofox_fcst_ts_stats_agg alias
    AggregateFunctionSet anofox_stats_agg_set("anofox_fcst_ts_stats_agg");
    anofox_stats_agg_set.AddFunction(agg_func);
    {
        CreateAggregateFunctionInfo info(anofox_stats_agg_set);
        info.alias_of = "ts_stats_agg";
        FunctionDescription desc;
        desc.description = "Aggregate function: computes 36 time series statistics for each group (including n_gap_runs). Returns a STRUCT with mean, std, min, max, trend, seasonality, and many more.";
        desc.examples = {"SELECT product_id, anofox_fcst_ts_stats_agg(date, value) FROM sales GROUP BY product_id"};
        desc.categories = {"time-series", "statistics"};
        desc.parameter_names = {"date", "value"};
        desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
