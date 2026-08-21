#pragma once

#include "duckdb.hpp"

namespace duckdb {

void RegisterTsForecastVarNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
