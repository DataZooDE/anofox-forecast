#pragma once

#include "duckdb.hpp"

namespace duckdb {

AggregateFunction CreateTSFeaturesFunction(const LogicalType &timestamp_type, bool has_config);

void RegisterTSFeaturesFunction(ExtensionLoader &loader);

} // namespace duckdb
