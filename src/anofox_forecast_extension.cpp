#define DUCKDB_EXTENSION_MAIN

#include "anofox_forecast_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

// Include our forecast functions
#include "forecast_table_function.hpp"
#include "forecast_aggregate.hpp"
#include "metrics_function.hpp"

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
    // std::cerr << "[DEBUG] Loading anofox_forecast extension..." << std::endl;
    
    // Register the FORECAST table function (for simple usage)
    auto forecast_function = CreateForecastTableFunction();
    loader.RegisterFunction(*forecast_function);
    // std::cerr << "[DEBUG] FORECAST table function registered" << std::endl;
    
    // Register the TS_FORECAST aggregate function (for GROUP BY usage)
    auto ts_forecast_agg = CreateTSForecastAggregate();
    loader.RegisterFunction(ts_forecast_agg);
    // std::cerr << "[DEBUG] TS_FORECAST aggregate function registered" << std::endl;
    
    // Register the TS_METRICS scalar function (for evaluation)
    RegisterMetricsFunction(loader);
    // std::cerr << "[DEBUG] TS_METRICS function registered" << std::endl;
    
    // std::cerr << "[DEBUG] All functions registered successfully" << std::endl;
}

void AnofoxForecastExtension::Load(ExtensionLoader &loader) {
    // std::cerr << "[DEBUG] AnofoxForecastExtension::Load called" << std::endl;
    LoadInternal(loader);
}

std::string AnofoxForecastExtension::Name() {
    return "anofox_forecast";
}

std::string AnofoxForecastExtension::Version() const {
#ifdef EXT_VERSION_ANOFOX_FORECAST
    return EXT_VERSION_ANOFOX_FORECAST;
#else
    return "1.0.0";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(anofox_forecast, loader) {
    // std::cerr << "[DEBUG] C extension entry point called" << std::endl;
    duckdb::LoadInternal(loader);
}
}