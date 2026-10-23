#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_changepoints_params.hpp"
#include "ts_fill_gaps_native.hpp"
#include "ts_list_marshal.hpp"
#include "ts_collect_finalize.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <map>
#include <mutex>
#include <set>
#include <atomic>
#include <thread>

namespace duckdb {

static LogicalType GetChangepointResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("changepoints", LogicalType::LIST(LogicalType(LogicalTypeId::UBIGINT))));
    children.push_back(make_pair("n_changepoints", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("cost", LogicalType(LogicalTypeId::DOUBLE)));
    return LogicalType::STRUCT(std::move(children));
}

// ============================================================================
// The two never-registered scalar bodies that used to live here (a one-list,
// a three-arg list+min_size+penalty variant) and their shared private list
// helper were deleted in Phase 12 plan 12-03 (D-01) — the live SQL surface is
// the ts_detect_changepoints table macro in ts_macros.cpp dispatching to
// _ts_detect_changepoints_bocpd. The no-op scalar registration function for
// this never-registered body was removed in plan 12-07 (D-09).
// ============================================================================

// ============================================================================
// BOCPD version - C++ API compatible
// Returns: STRUCT(is_changepoint BOOLEAN[], changepoint_probability DOUBLE[])
// ============================================================================

static LogicalType GetBocpdResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("is_changepoint", LogicalType::LIST(LogicalType(LogicalTypeId::BOOLEAN))));
    children.push_back(make_pair("changepoint_probability", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("changepoint_indices", LogicalType::LIST(LogicalType(LogicalTypeId::UBIGINT))));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDetectChangepointsBocpdFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &lambda_vec = args.data[1];
    auto &probs_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    ListDoubleReader values_reader(list_vec, count);
    ScalarArgReader<double> lambda_reader(lambda_vec, count);
    ScalarArgReader<bool> probs_reader(probs_vec, count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        vector<double> values;
        if (!values_reader.ReadSingleSeries(row_idx, values, "_ts_detect_changepoints_bocpd")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Handle too-small arrays - need at least 2 points for changepoint detection
        if (values.size() < 2) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        double hazard_lambda = lambda_reader.IsNull(row_idx) ? 250.0 : lambda_reader.Get(row_idx);
        bool include_probs = probs_reader.IsNull(row_idx) ? false : probs_reader.Get(row_idx);

        BocpdResult bocpd_result;
        memset(&bocpd_result, 0, sizeof(bocpd_result));
        AnofoxError error;

        bool success = anofox_ts_detect_changepoints_bocpd(
            values.data(),
            values.size(),
            hazard_lambda,
            include_probs,
            &bocpd_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set is_changepoint list
        {
            auto &cp_list = StructEntry(children[0]);
            auto list_data = FlatDataMut<list_entry_t>(cp_list);
            auto &list_child = ListChildMut(cp_list);
            auto current_size = ListVector::GetListSize(cp_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = bocpd_result.n_points;

            ListVector::Reserve(cp_list, current_size + bocpd_result.n_points);
            ListVector::SetListSize(cp_list, current_size + bocpd_result.n_points);

            auto child_data = FlatDataMut<bool>(list_child);
            if (bocpd_result.is_changepoint) {
                for (size_t i = 0; i < bocpd_result.n_points; i++) {
                    child_data[current_size + i] = bocpd_result.is_changepoint[i];
                }
            }
        }

        // Set changepoint_probability list
        {
            auto &prob_list = StructEntry(children[1]);
            auto list_data = FlatDataMut<list_entry_t>(prob_list);
            auto &list_child = ListChildMut(prob_list);
            auto current_size = ListVector::GetListSize(prob_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = bocpd_result.n_points;

            ListVector::Reserve(prob_list, current_size + bocpd_result.n_points);
            ListVector::SetListSize(prob_list, current_size + bocpd_result.n_points);

            auto child_data = FlatDataMut<double>(list_child);
            if (bocpd_result.changepoint_probability) {
                for (size_t i = 0; i < bocpd_result.n_points; i++) {
                    child_data[current_size + i] = bocpd_result.changepoint_probability[i];
                }
            }
        }

        // Set changepoint_indices list
        {
            auto &idx_list = StructEntry(children[2]);
            auto list_data = FlatDataMut<list_entry_t>(idx_list);
            auto &list_child = ListChildMut(idx_list);
            auto current_size = ListVector::GetListSize(idx_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = bocpd_result.n_changepoints;

            ListVector::Reserve(idx_list, current_size + bocpd_result.n_changepoints);
            ListVector::SetListSize(idx_list, current_size + bocpd_result.n_changepoints);

            auto child_data = FlatDataMut<uint64_t>(list_child);
            if (bocpd_result.changepoint_indices) {
                for (size_t i = 0; i < bocpd_result.n_changepoints; i++) {
                    child_data[current_size + i] = bocpd_result.changepoint_indices[i];
                }
            }
        }

        anofox_free_bocpd_result(&bocpd_result);
    }
}

void RegisterTsDetectChangepointsBocpdFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_detect_changepoints table macros
    ScalarFunctionSet ts_bocpd_set("_ts_detect_changepoints_bocpd");

    // _ts_detect_changepoints_bocpd(values, hazard_lambda, include_probabilities)
    ts_bocpd_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::BOOLEAN)},
        GetBocpdResultType(),
        TsDetectChangepointsBocpdFunction
    )));

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_bocpd_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

// ============================================================================
// ts_detect_changepoints_by - Native Table Function
// ============================================================================
// Returns row-level changepoint detection results with preserved column names.
// Output: group_col (preserved name), date_col, is_changepoint, changepoint_probability

struct TsDetectChangepointsByBindData : public TableFunctionData {
    ChangepointParamSet params;
    bool include_probabilities = true;  // Always include for row-level output
    string group_col_name;
    string date_col_name;
    string value_col_name;
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Group Data and Result Structures (standalone for GlobalState access)
// ============================================================================

struct ChangepointGroupData {
    Value group_value;
    vector<int64_t> timestamps;
    vector<double> values;
};

struct ChangepointNullDateRow {
    Value group_value;
};

struct ChangepointOutputRow {
    Value group_value;
    int64_t timestamp;
    bool is_changepoint;
    double changepoint_probability;
    bool changepoint_probability_null = false;
    bool date_is_null = false;
};

// ============================================================================
// Global State - thread-safe group collection + single-thread finalize
// ============================================================================

struct TsDetectChangepointsByGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override {
        return 999999;
    }

    // Thread-safe group storage (moved from LocalState)
    std::mutex groups_mutex;
    std::map<string, ChangepointGroupData> groups;
    vector<string> group_order;
    vector<ChangepointNullDateRow> null_date_rows;

    // Processing results (used by finalize owner)
    vector<ChangepointOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;

    // Single-thread finalize: last-finalizer barrier (ts_collect_finalize.hpp)
    TsCollectFinalizeBarrier barrier;
};

struct TsDetectChangepointsByLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool arrived_at_final = false;
};

static unique_ptr<FunctionData> TsDetectChangepointsByBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    TableBindNames &names) {

    auto bind_data = make_uniq<TsDetectChangepointsByBindData>();

    // Parse params from second argument (MAP(VARCHAR, VARCHAR)): single
    // shared parser with ts_detect_changepoints_agg (#287, BUG-06, Phase 20
    // 20-06), so both consumers agree on hazard_lambda for identical inputs.
    if (input.inputs.size() >= 2) {
        bind_data->params = ParseChangepointParamSet(input.inputs[1]);
    }

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "ts_detect_changepoints_by requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Preserve column names
    bind_data->group_col_name = NameString(input.input_table_names[0]);
    bind_data->date_col_name = NameString(input.input_table_names[1]);
    bind_data->value_col_name = NameString(input.input_table_names[2]);
    bind_data->group_logical_type = input.input_table_types[0];
    bind_data->date_logical_type = input.input_table_types[1];

    // Detect date column type
    switch (input.input_table_types[1].id()) {
        case LogicalTypeId::DATE:
            bind_data->date_col_type = DateColumnType::DATE;
            break;
        case LogicalTypeId::TIMESTAMP:
        case LogicalTypeId::TIMESTAMP_TZ:
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
            break;
        default:
            throw InvalidInputException(
                "Date column must be DATE or TIMESTAMP, got: %s",
                input.input_table_types[1].ToString().c_str());
    }

    // Output schema: group_col, date_col, is_changepoint, changepoint_probability
    names.push_back(MakeBindName(bind_data->group_col_name));
    return_types.push_back(bind_data->group_logical_type);

    names.push_back(MakeBindName(bind_data->date_col_name));
    return_types.push_back(bind_data->date_logical_type);

    names.push_back(MakeBindName("is_changepoint"));
    return_types.push_back(LogicalType(LogicalTypeId::BOOLEAN));

    names.push_back(MakeBindName("changepoint_probability"));
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsDetectChangepointsByInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsDetectChangepointsByGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsDetectChangepointsByInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    global_state->Cast<TsDetectChangepointsByGlobalState>().barrier.RegisterLocal();
    return make_uniq<TsDetectChangepointsByLocalState>();
}

static OperatorResultType TsDetectChangepointsByInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsDetectChangepointsByBindData>();
    auto &gstate = data_p.global_state->Cast<TsDetectChangepointsByGlobalState>();

    // Late-row guard: rows after the finalize claim would be silently dropped.
    gstate.barrier.AssertCollecting("_ts_detect_changepoints_by_native");

    // Extract batch locally (no lock)
    struct TempRow {
        Value group_val;
        string group_key;
        int64_t date_micros;
        double value;
    };
    vector<TempRow> batch;
    vector<ChangepointNullDateRow> null_batch;

    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) {
            ChangepointNullDateRow null_row;
            null_row.group_value = group_val;
            null_batch.push_back(null_row);
            continue;
        }

        TempRow row;
        row.group_val = group_val;
        row.group_key = GetGroupKey(group_val);

        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                row.date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
            default:
                row.date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
        }

        row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();
        batch.push_back(std::move(row));
    }

    // Lock once, insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &nr : null_batch) {
            gstate.null_date_rows.push_back(std::move(nr));
        }
        for (auto &row : batch) {
            if (gstate.groups.find(row.group_key) == gstate.groups.end()) {
                gstate.groups[row.group_key] = ChangepointGroupData();
                gstate.groups[row.group_key].group_value = row.group_val;
                gstate.group_order.push_back(row.group_key);
            }
            auto &grp = gstate.groups[row.group_key];
            grp.timestamps.push_back(row.date_micros);
            grp.values.push_back(row.value);
        }
    }

    SetOutputCardinality(output, 0);
    return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType TsDetectChangepointsByFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsDetectChangepointsByBindData>();
    auto &gstate = data_p.global_state->Cast<TsDetectChangepointsByGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsDetectChangepointsByLocalState>();

    // Last-finalizer claim (ts_collect_finalize.hpp): the local state whose
    // arrival completes the count of registered local states processes; every
    // other one finishes at once (no spin-wait).
    if (!lstate.owns_finalize) {
        if (lstate.arrived_at_final) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.arrived_at_final = true;
        if (!gstate.barrier.ArriveAtFinal()) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.owns_finalize = true;
    }

    // Process all groups (single thread)
    if (!gstate.processed) {
        // Emit warning if there are NULL date rows
        if (!gstate.null_date_rows.empty()) {
            string warning = StringUtil::Format(
                "ts_detect_changepoints_by: %zu rows with NULL dates - these will have is_changepoint=false, probability=NULL",
                gstate.null_date_rows.size()
            );
            Printer::Print(warning);
        }

        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];

            // Handle groups with < 2 points: output with default values instead of skipping
            if (grp.values.size() < 2) {
                for (size_t i = 0; i < grp.timestamps.size(); i++) {
                    ChangepointOutputRow row;
                    row.group_value = grp.group_value;
                    row.timestamp = grp.timestamps[i];
                    row.is_changepoint = false;
                    row.changepoint_probability = 0.0;
                    row.changepoint_probability_null = true;
                    gstate.results.push_back(row);
                }
                continue;
            }

            // Sort by timestamp while preserving correspondence
            vector<pair<int64_t, double>> sorted_data;
            for (size_t i = 0; i < grp.timestamps.size(); i++) {
                sorted_data.push_back({grp.timestamps[i], grp.values[i]});
            }
            std::sort(sorted_data.begin(), sorted_data.end());

            vector<double> sorted_values;
            vector<int64_t> sorted_timestamps;
            for (const auto &p : sorted_data) {
                sorted_timestamps.push_back(p.first);
                sorted_values.push_back(p.second);
            }

            // Call Rust FFI (#287, BUG-06, D2-ii): shared options struct so
            // both consumers decide is_changepoint identically for
            // identical hazard_lambda/threshold.
            BocpdResult bocpd_result = {};
            AnofoxError error = {};

            ChangepointOptionsFFI opts;
            opts.hazard_lambda = bind_data.params.hazard_lambda;
            opts.threshold = bind_data.params.threshold;
            opts.deseasonalize = bind_data.params.deseasonalize;
            opts.seasonal_period = bind_data.params.seasonal_period;

            bool success = anofox_ts_detect_changepoints_bocpd_opts(
                sorted_values.data(),
                sorted_values.size(),
                &opts,
                &bocpd_result,
                &error
            );

            if (!success) {
                if (error.code == INVALID_INPUT) {
                    throw InvalidInputException("ts_detect_changepoints_by: %s",
                        error.message ? error.message : "invalid input");
                }
                // Output rows with default values on error instead of skipping
                for (size_t i = 0; i < sorted_values.size(); i++) {
                    ChangepointOutputRow row;
                    row.group_value = grp.group_value;
                    row.timestamp = sorted_timestamps[i];
                    row.is_changepoint = false;
                    row.changepoint_probability = 0.0;
                    row.changepoint_probability_null = true;
                    gstate.results.push_back(row);
                }
                continue;
            }

            // Create output rows
            for (size_t i = 0; i < bocpd_result.n_points; i++) {
                ChangepointOutputRow row;
                row.group_value = grp.group_value;
                row.timestamp = sorted_timestamps[i];
                row.is_changepoint = bocpd_result.is_changepoint ? bocpd_result.is_changepoint[i] : false;
                row.changepoint_probability = bocpd_result.changepoint_probability ? bocpd_result.changepoint_probability[i] : 0.0;
                gstate.results.push_back(row);
            }

            anofox_free_bocpd_result(&bocpd_result);
        }

        // Output NULL date rows with default values
        for (const auto &null_row : gstate.null_date_rows) {
            ChangepointOutputRow row;
            row.group_value = null_row.group_value;
            row.timestamp = 0;  // Placeholder, will be output as NULL
            row.is_changepoint = false;
            row.changepoint_probability = 0.0;
            row.changepoint_probability_null = true;
            row.date_is_null = true;
            gstate.results.push_back(row);
        }

        gstate.processed = true;
    }

    // Output results
    if (gstate.results.empty() || gstate.output_offset >= gstate.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    while (output_count < STANDARD_VECTOR_SIZE && gstate.output_offset < gstate.results.size()) {
        auto &row = gstate.results[gstate.output_offset];
        idx_t out_idx = output_count;

        // Group column
        output.data[0].SetValue(out_idx, row.group_value);

        // Date column - output NULL for NULL date rows
        if (row.date_is_null) {
            FlatVector::SetNull(output.data[1], out_idx, true);
        } else {
            switch (bind_data.date_col_type) {
                case DateColumnType::DATE:
                    output.data[1].SetValue(out_idx, Value::DATE(MicrosecondsToDate(row.timestamp)));
                    break;
                case DateColumnType::TIMESTAMP:
                default:
                    output.data[1].SetValue(out_idx, Value::TIMESTAMP(MicrosecondsToTimestamp(row.timestamp)));
                    break;
            }
        }

        // is_changepoint
        FlatDataMut<bool>(output.data[2])[out_idx] = row.is_changepoint;

        // changepoint_probability - output NULL when flagged
        if (row.changepoint_probability_null) {
            FlatVector::SetNull(output.data[3], out_idx, true);
        } else {
            FlatDataMut<double>(output.data[3])[out_idx] = row.changepoint_probability;
        }

        output_count++;
        gstate.output_offset++;
    }

    SetOutputCardinality(output, output_count);

    if (gstate.output_offset >= gstate.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

void RegisterTsDetectChangepointsByFunction(ExtensionLoader &loader) {
    // Internal native table function: _ts_detect_changepoints_by_native(TABLE, params)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Called by ts_detect_changepoints_by SQL macro
    TableFunction func("_ts_detect_changepoints_by_native",
        {LogicalType::TABLE, LogicalType::MAP(LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR))},
        nullptr,
        TsDetectChangepointsByBind,
        TsDetectChangepointsByInitGlobal,
        TsDetectChangepointsByInitLocal);

    func.in_out_function = TsDetectChangepointsByInOut;
    func.in_out_function_final = TsDetectChangepointsByFinalize;

    loader.RegisterFunction(func);
}

// Note: RegisterTsDetectChangepointsAggFunction is implemented in ts_changepoints_agg.cpp

} // namespace duckdb
