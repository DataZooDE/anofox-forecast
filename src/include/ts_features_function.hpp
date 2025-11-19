#pragma once

#include "duckdb.hpp"

namespace duckdb {

AggregateFunction CreateTSFeaturesFunction(const LogicalType &timestamp_type);

void RegisterTSFeaturesFunction(ExtensionLoader &loader);

} // namespace duckdb
