#include "duckdb_compat.hpp"
#include "anofox_forecast_extension.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

// ============================================================================
// _ts_passthrough(TABLE) - internal streaming identity in-out function (#285)
//
// Why it exists: the metric macros must put a pipeline breaker over their `source` so
// the collect-then-finalize natives see a single input pipeline (a UNION ALL
// source otherwise makes them finalize on part of the rows; see
// ts_collect_finalize.hpp). A table macro cannot write `FROM source` when
// `source` is a subquery argument (macro parameters substitute only in
// expression positions), but it can pass it to a table function. So the macros
// write
//     _ts_..._native((SELECT * FROM _ts_passthrough(source) ORDER BY ALL), ...)
//
// It must stay STREAMING: no in_out_function_final and no state, so it is
// correct under any number of input pipelines. It references every input
// column into the output chunk unchanged.
// ============================================================================

static unique_ptr<FunctionData> TsPassthroughBind(ClientContext &context, TableFunctionBindInput &input,
                                                  vector<LogicalType> &return_types, TableBindNames &names) {
    for (idx_t i = 0; i < input.input_table_types.size(); i++) {
        return_types.push_back(input.input_table_types[i]);
        names.push_back(MakeBindName(NameString(input.input_table_names[i])));
    }
    return make_uniq<TableFunctionData>();
}

static OperatorResultType TsPassthroughInOut(ExecutionContext &context, TableFunctionInput &data, DataChunk &input,
                                             DataChunk &output) {
    for (idx_t col = 0; col < input.ColumnCount(); col++) {
        output.data[col].Reference(input.data[col]);
    }
    SetOutputCardinality(output, input.size());
    return OperatorResultType::NEED_MORE_INPUT;
}

void RegisterTsPassthroughFunction(ExtensionLoader &loader) {
    TableFunction func("_ts_passthrough", {LogicalType::TABLE}, nullptr, TsPassthroughBind);
    func.in_out_function = TsPassthroughInOut;
    loader.RegisterFunction(func);
}

} // namespace duckdb
