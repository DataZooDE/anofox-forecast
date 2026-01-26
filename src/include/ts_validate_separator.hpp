#pragma once

#include "duckdb.hpp"

namespace duckdb {

void RegisterTsValidateSeparatorFunction(ExtensionLoader &loader);

} // namespace duckdb
