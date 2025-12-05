#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

// Register the test_batch_index_reproduction operator for demonstrating
// the batch index collision bug in DuckDB's PhysicalBatchInsert::AddCollection
//
// This operator mimics the Table-In-Out pattern with a CPU-intensive Final phase
// to expose race conditions when multiple threads complete at variable times.
//
// Usage:
//   CREATE TABLE test AS SELECT * FROM test_batch_index_reproduction(
//       TABLE large_data, 'group_col', 'value_col', 100);
//
// Parameters:
//   - table: Input table (TABLE type)
//   - group_col: Column name for grouping (VARCHAR)
//   - value_col: Column name for values (VARCHAR)
//   - delay_ms: Artificial delay per group to simulate computation (INTEGER)
//
// Expected behavior:
//   - Works with small datasets (e.g., 10 groups)
//   - Fails with large datasets (e.g., 10,000+ groups) and delay > 0ms
//   - Error: "batch index 9999999999999 is present in multiple collections"
void RegisterTestBatchIndexReproduction(ExtensionLoader &loader);

} // namespace duckdb
