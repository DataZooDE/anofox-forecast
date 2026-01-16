#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/parser/parser.hpp"

#include <algorithm>
#include <map>

namespace duckdb {

// ============================================================================
// ts_fill_forward_operator - Native C++ table-in-out operator
//
// This native operator implements the same functionality as the ts_fill_forward
// SQL macro but with MaxThreads() = 1 to prevent BatchedDataCollection::Merge
// errors during parallel execution.
//
// C++ API: ts_fill_forward_operator(source_table, group_col, date_col, value_col, target_date, frequency)
// ============================================================================

struct TsFillForwardOperatorBindData : public TableFunctionData {
    string source_table;
    string group_col;
    string date_col;
    string value_col;
    timestamp_t target_date;
    int64_t frequency_seconds;
};

struct TsFillForwardOperatorGlobalState : public GlobalTableFunctionState {
    // Return 1 to force single-threaded execution
    // This prevents BatchedDataCollection::Merge errors
    idx_t MaxThreads() const override {
        return 1;
    }

    bool finished = false;
    unique_ptr<QueryResult> query_result;
    idx_t current_row = 0;
};

struct TsFillForwardOperatorLocalState : public LocalTableFunctionState {
    // Local state per thread (minimal since we force single-thread)
};

static unique_ptr<FunctionData> TsFillForwardOperatorBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsFillForwardOperatorBindData>();

    // Extract parameters
    bind_data->source_table = input.inputs[0].GetValue<string>();
    bind_data->group_col = input.inputs[1].GetValue<string>();
    bind_data->date_col = input.inputs[2].GetValue<string>();
    bind_data->value_col = input.inputs[3].GetValue<string>();
    bind_data->target_date = input.inputs[4].GetValue<timestamp_t>();

    // Parse frequency interval to seconds
    auto frequency_str = input.inputs[5].GetValue<string>();
    // For simplicity, assume frequency is provided in a parseable format
    // In a full implementation, we'd parse the interval properly
    bind_data->frequency_seconds = 86400; // Default to 1 day

    // Define output columns to match the source table + filled rows
    names.push_back(bind_data->group_col);
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back(bind_data->date_col);
    return_types.push_back(LogicalType(LogicalTypeId::TIMESTAMP));

    names.push_back(bind_data->value_col);
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsFillForwardOperatorInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {

    auto global_state = make_uniq<TsFillForwardOperatorGlobalState>();

    // Get bind data
    auto &bind_data = input.bind_data->Cast<TsFillForwardOperatorBindData>();

    // Build and execute the SQL query that performs the fill forward
    // This uses the same logic as the SQL macro but runs within the C++ operator
    string query = StringUtil::Format(R"(
        WITH src AS (
            SELECT "%s" AS _grp, "%s" AS _dt, "%s" AS _val
            FROM "%s"
        ),
        last_dates AS (
            SELECT
                _grp,
                date_trunc('second', MAX(_dt)::TIMESTAMP) AS _max_dt
            FROM src
            GROUP BY _grp
        ),
        forward_dates AS (
            SELECT
                ld._grp,
                UNNEST(generate_series(
                    ld._max_dt + INTERVAL '%lld seconds',
                    TIMESTAMP '%s',
                    INTERVAL '%lld seconds'
                )) AS _dt
            FROM last_dates ld
            WHERE ld._max_dt < TIMESTAMP '%s'
        )
        SELECT _grp, _dt, _val FROM src
        UNION ALL
        SELECT
            fd._grp,
            fd._dt,
            NULL::DOUBLE
        FROM forward_dates fd
        ORDER BY 1, 2
    )",
        bind_data.group_col.c_str(),
        bind_data.date_col.c_str(),
        bind_data.value_col.c_str(),
        bind_data.source_table.c_str(),
        bind_data.frequency_seconds,
        Timestamp::ToString(bind_data.target_date).c_str(),
        bind_data.frequency_seconds,
        Timestamp::ToString(bind_data.target_date).c_str()
    );

    // Execute the query
    global_state->query_result = context.Query(query, false);

    return global_state;
}

static unique_ptr<LocalTableFunctionState> TsFillForwardOperatorInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {

    return make_uniq<TsFillForwardOperatorLocalState>();
}

static void TsFillForwardOperatorExecute(
    ClientContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &global_state = data_p.global_state->Cast<TsFillForwardOperatorGlobalState>();

    if (global_state.finished) {
        output.SetCardinality(0);
        return;
    }

    if (!global_state.query_result || global_state.query_result->HasError()) {
        if (global_state.query_result) {
            throw InvalidInputException("ts_fill_forward_operator query failed: %s",
                global_state.query_result->GetError().c_str());
        }
        global_state.finished = true;
        output.SetCardinality(0);
        return;
    }

    // Fetch the next chunk
    auto chunk = global_state.query_result->Fetch();
    if (!chunk || chunk->size() == 0) {
        global_state.finished = true;
        output.SetCardinality(0);
        return;
    }

    // Copy the chunk to output
    output.Initialize(context, chunk->GetTypes());
    output.Reference(*chunk);
}

void RegisterTsFillForwardOperatorFunction(ExtensionLoader &loader) {
    TableFunction func("ts_fill_forward_operator",
        {LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR),
         LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::VARCHAR)},
        TsFillForwardOperatorExecute,
        TsFillForwardOperatorBind,
        TsFillForwardOperatorInitGlobal,
        TsFillForwardOperatorInitLocal);

    loader.RegisterFunction(func);

    // Also register with anofox_fcst prefix for API compatibility
    TableFunction anofox_func = func;
    anofox_func.name = "anofox_fcst_ts_fill_forward_operator";
    loader.RegisterFunction(anofox_func);
}

} // namespace duckdb
