#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include "ts_list_marshal.hpp"

// Internal scalars backing the Rust-delegated statistics macros (Phase 12
// RUST-03/RUST-05/D-09, plan 12-07): ts_diff_by, ts_fill_nulls_mean_by, and
// ts_drop_constant_by dispatch to _ts_diff / _ts_fill_nulls_mean /
// _ts_is_constant here instead of computing the statistic in SQL.

namespace duckdb {

// ============================================================================
// _ts_diff(LIST(DOUBLE), diff_order INTEGER, seasonal_lag INTEGER) -> LIST(DOUBLE)
//
// d-th order, lag-`seasonal_lag` differencing via the Rust diff_aligned
// (Phase 12 D-05/D-06): output has the SAME length as the input list, with
// the first diff_order*seasonal_lag entries NULL, and any difference
// touching a missing input NULL (never an error — the SQL row-wise-transform
// policy, not the single-series interior-NULL error rule D-03).
// ============================================================================

static void TsDiffExecute(DataChunk &args, ExpressionState &state, Vector &result) {
	idx_t count = args.size();

	ListDoubleReader list_reader(args.data[0], count);
	ScalarArgReader<int32_t> order_reader(args.data[1], count);
	ScalarArgReader<int32_t> lag_reader(args.data[2], count);

	result.SetVectorType(VectorType::FLAT_VECTOR);

	vector<double> values;
	vector<uint64_t> validity;

	for (idx_t row_idx = 0; row_idx < count; row_idx++) {
		if (list_reader.IsNull(row_idx)) {
			FlatVector::SetNull(result, row_idx, true);
			continue;
		}
		if (order_reader.IsNull(row_idx)) {
			throw InvalidInputException("_ts_diff: diff_order must not be NULL");
		}
		if (lag_reader.IsNull(row_idx)) {
			throw InvalidInputException("_ts_diff: seasonal_lag must not be NULL");
		}

		idx_t n = list_reader.Read(row_idx, values, validity);
		int32_t order = order_reader.Get(row_idx);
		int32_t lag = lag_reader.Get(row_idx);

		if (n == 0) {
			auto list_entries = FlatVector::GetData<list_entry_t>(result);
			list_entries[row_idx].offset = ListVector::GetListSize(result);
			list_entries[row_idx].length = 0;
			continue;
		}

		vector<double> out_values(n);
		vector<uint64_t> out_validity((n + 63) / 64);
		AnofoxError error = {};

		bool success = anofox_ts_diff(values.empty() ? nullptr : values.data(),
		                               validity.empty() ? nullptr : validity.data(), n, order, lag,
		                               out_values.empty() ? nullptr : out_values.data(),
		                               out_validity.empty() ? nullptr : out_validity.data(), &error);

		if (!success) {
			throw InvalidInputException("_ts_diff: %s", error.message);
		}

		WriteDoubleListWithValidity(result, row_idx, out_values.data(), n,
		                             out_validity.empty() ? nullptr : out_validity.data());
	}
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsSeriesTransformFunctions(ExtensionLoader &loader) {
	ScalarFunction diff_func(
	    "_ts_diff", {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::INTEGER},
	    LogicalType::LIST(LogicalType::DOUBLE), TsDiffExecute);
	diff_func.null_handling = FunctionNullHandling::SPECIAL_HANDLING;

	CreateScalarFunctionInfo diff_info(diff_func);
	diff_info.internal = true;
	loader.RegisterFunction(diff_info);
}

} // namespace duckdb
