#pragma once

#include "duckdb.hpp"

namespace duckdb {

void RegisterTsForecastPanelNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
