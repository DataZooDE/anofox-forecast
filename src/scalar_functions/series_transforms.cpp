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
			auto list_entries = FlatDataMut<list_entry_t>(result);
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
// _ts_fill_nulls_mean(LIST(DOUBLE)) -> LIST(DOUBLE)
//
// Mean fill via the Rust fill_nulls_mean (Phase 12 RUST-05/D-09): NaN is
// treated as missing (Phase 10 NaN = NULL convention); an all-missing group
// stays NaN everywhere, written out as SQL NULL.
// ============================================================================

static void TsFillNullsMeanExecute(DataChunk &args, ExpressionState &state, Vector &result) {
	idx_t count = args.size();

	ListDoubleReader list_reader(args.data[0], count);

	result.SetVectorType(VectorType::FLAT_VECTOR);

	vector<double> values;
	vector<uint64_t> validity;

	for (idx_t row_idx = 0; row_idx < count; row_idx++) {
		if (list_reader.IsNull(row_idx)) {
			FlatVector::SetNull(result, row_idx, true);
			continue;
		}

		idx_t n = list_reader.Read(row_idx, values, validity);

		if (n == 0) {
			auto list_entries = FlatDataMut<list_entry_t>(result);
			list_entries[row_idx].offset = ListVector::GetListSize(result);
			list_entries[row_idx].length = 0;
			continue;
		}

		double *out_values = nullptr;
		AnofoxError error = {};

		bool success = anofox_ts_fill_nulls_mean(values.data(), validity.empty() ? nullptr : validity.data(), n,
		                                          &out_values, &error);

		if (!success) {
			throw InvalidInputException("_ts_fill_nulls_mean: %s", error.message);
		}

		WriteDoubleListNanAsNull(result, row_idx, out_values, n);
		anofox_free_double_array(out_values);
	}
}

// ============================================================================
// _ts_is_constant(LIST(DOUBLE)) -> BOOLEAN
//
// Rust is_constant (Phase 12 D-09): fewer than two non-missing values
// (including an all-missing/empty group) classifies as constant.
// ============================================================================

static void TsIsConstantExecute(DataChunk &args, ExpressionState &state, Vector &result) {
	idx_t count = args.size();

	ListDoubleReader list_reader(args.data[0], count);

	result.SetVectorType(VectorType::FLAT_VECTOR);

	vector<double> values;
	vector<uint64_t> validity;

	for (idx_t row_idx = 0; row_idx < count; row_idx++) {
		if (list_reader.IsNull(row_idx)) {
			FlatVector::SetNull(result, row_idx, true);
			continue;
		}

		idx_t n = list_reader.Read(row_idx, values, validity);

		bool is_const = true;
		AnofoxError error = {};
		bool success = anofox_ts_is_constant(n == 0 ? nullptr : values.data(),
		                                      (n == 0 || validity.empty()) ? nullptr : validity.data(), n,
		                                      &is_const, &error);

		if (!success) {
			throw InvalidInputException("_ts_is_constant: %s", error.message);
		}

		FlatDataMut<bool>(result)[row_idx] = is_const;
	}
}

// ============================================================================
// _ts_params_map(params ANY) -> MAP(VARCHAR, VARCHAR)
// The SQL macros read option values with params['key']. A cast to
// MAP(VARCHAR, VARCHAR) handles STRUCT and MAP, but not VARIANT (no
// VARIANT -> MAP cast exists); this goes through the same normalization as
// the C++ parsers, without depending on the json extension.
// ============================================================================

static void TsParamsMapExecute(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &params = args.data[0];
	for (idx_t i = 0; i < args.size(); i++) {
		result.SetValue(i, ParamsAsVarcharMap(params.GetValue(i)));
	}
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsSeriesTransformFunctions(ExtensionLoader &loader) {
	ScalarFunction params_map_func("_ts_params_map", {LogicalType::ANY},
	                               LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR), TsParamsMapExecute);
	MarkFallible(params_map_func);
	SetNullHandlingOf(params_map_func, FunctionNullHandling::SPECIAL_HANDLING);

	CreateScalarFunctionInfo params_map_info(params_map_func);
	params_map_info.internal = true;
	loader.RegisterFunction(params_map_info);

	ScalarFunction diff_func(
	    "_ts_diff", {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::INTEGER},
	    LogicalType::LIST(LogicalType::DOUBLE), TsDiffExecute);
	MarkFallible(diff_func);
	SetNullHandlingOf(diff_func, FunctionNullHandling::SPECIAL_HANDLING);

	CreateScalarFunctionInfo diff_info(diff_func);
	diff_info.internal = true;
	loader.RegisterFunction(diff_info);

	ScalarFunction fill_nulls_mean_func("_ts_fill_nulls_mean", {LogicalType::LIST(LogicalType::DOUBLE)},
	                                     LogicalType::LIST(LogicalType::DOUBLE), TsFillNullsMeanExecute);
	MarkFallible(fill_nulls_mean_func);
	SetNullHandlingOf(fill_nulls_mean_func, FunctionNullHandling::SPECIAL_HANDLING);

	CreateScalarFunctionInfo fill_nulls_mean_info(fill_nulls_mean_func);
	fill_nulls_mean_info.internal = true;
	loader.RegisterFunction(fill_nulls_mean_info);

	ScalarFunction is_constant_func("_ts_is_constant", {LogicalType::LIST(LogicalType::DOUBLE)},
	                                 LogicalType::BOOLEAN, TsIsConstantExecute);
	MarkFallible(is_constant_func);
	SetNullHandlingOf(is_constant_func, FunctionNullHandling::SPECIAL_HANDLING);

	CreateScalarFunctionInfo is_constant_info(is_constant_func);
	is_constant_info.internal = true;
	loader.RegisterFunction(is_constant_info);
}

} // namespace duckdb
