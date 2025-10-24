#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "model_factory.hpp"
#include "time_series_builder.hpp"
#include <memory>
#include <vector>
#include <chrono>

namespace duckdb {

// Forward declarations
namespace anofoxtime {
namespace core {
    class TimeSeries;
    struct Forecast;
}
namespace models {
    class IForecaster;
}
}

// Bind data for the FORECAST function
struct ForecastBindData : public TableFunctionData {
    string model_name;
    int32_t horizon;
    Value model_params;
    idx_t timestamp_col_idx = 0;
    idx_t value_col_idx = 1;
    
    ForecastBindData() = default;
};

// Global state for the FORECAST function
struct ForecastGlobalState : public GlobalTableFunctionState {
    const ForecastBindData* bind_data = nullptr;
    idx_t timestamp_col_idx = 0;
    idx_t value_col_idx = 1;
    
    ForecastGlobalState() = default;
};

// Local state for the FORECAST function
struct ForecastLocalState : public LocalTableFunctionState {
    vector<std::chrono::system_clock::time_point> timestamps;
    vector<double> values;
    bool input_done = false;
    bool forecast_generated = false;
    idx_t output_offset = 0;
    ::anofoxtime::models::IForecaster* model = nullptr;
    ::anofoxtime::core::Forecast* forecast = nullptr;
    std::chrono::high_resolution_clock::time_point fit_start_time;
    
    ForecastLocalState() = default;
    ~ForecastLocalState(); // Defined in .cpp where full types are available
};

// Function declarations
unique_ptr<FunctionData> ForecastBind(ClientContext &context, TableFunctionBindInput &input, 
                                     vector<LogicalType> &return_types, vector<string> &names);

unique_ptr<GlobalTableFunctionState> ForecastInitGlobal(ClientContext &context, 
                                                       TableFunctionInitInput &input);

unique_ptr<LocalTableFunctionState> ForecastInitLocal(ExecutionContext &context, 
                                                     TableFunctionInitInput &input, 
                                                     GlobalTableFunctionState *global_state);

// Table-in-out function that receives input data row by row
OperatorResultType ForecastInOutFunction(ExecutionContext &context, TableFunctionInput &data_p, 
                                         DataChunk &input, DataChunk &output);

// Finalize function called when all input is processed
OperatorFinalizeResultType ForecastInOutFinal(ExecutionContext &context, TableFunctionInput &data_p, 
                                               DataChunk &output);

unique_ptr<NodeStatistics> ForecastCardinality(ClientContext &context, const FunctionData *bind_data);

// Create the FORECAST table function
unique_ptr<TableFunction> CreateForecastTableFunction();

} // namespace duckdb