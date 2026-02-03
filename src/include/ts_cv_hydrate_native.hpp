#pragma once

#include "duckdb.hpp"

namespace duckdb {

void RegisterTsCvHydrateNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
