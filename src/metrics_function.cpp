#include "metrics_function.hpp"
#include "anofox_time_wrapper.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/main/extension_entries.hpp"

// Include anofox-time metrics
#include "anofox-time/utils/metrics.hpp"

namespace duckdb {

// Helper function to extract vector<double> from LIST
static std::vector<double> ExtractDoubleArray(Vector &vec, idx_t index, UnifiedVectorFormat &format) {
	std::vector<double> result;

	auto list_entry = UnifiedVectorFormat::GetData<list_entry_t>(format)[index];
	auto &child = ListVector::GetEntry(vec);

	UnifiedVectorFormat child_format;
	child.ToUnifiedFormat(ListVector::GetListSize(vec), child_format);
	auto child_data = UnifiedVectorFormat::GetData<double>(child_format);

	for (idx_t i = 0; i < list_entry.length; i++) {
		auto idx = list_entry.offset + i;
		auto mapped_idx = child_format.sel->get_index(idx);
		if (child_format.validity.RowIsValid(mapped_idx)) {
			result.push_back(child_data[mapped_idx]);
		}
	}

	return result;
}

// TS_MAE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSMAEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];

	UnifiedVectorFormat actual_format, predicted_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		result_data[i] = ::anofoxtime::utils::Metrics::mae(actual, predicted);
	}
}

// TS_MSE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSMSEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];

	UnifiedVectorFormat actual_format, predicted_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		result_data[i] = ::anofoxtime::utils::Metrics::mse(actual, predicted);
	}
}

// TS_RMSE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSRMSEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];

	UnifiedVectorFormat actual_format, predicted_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		result_data[i] = ::anofoxtime::utils::Metrics::rmse(actual, predicted);
	}
}

// TS_MAPE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSMAPEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];

	UnifiedVectorFormat actual_format, predicted_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		auto mape = ::anofoxtime::utils::Metrics::mape(actual, predicted);
		if (mape.has_value()) {
			result_data[i] = mape.value();
		} else {
			result_validity.SetInvalid(i);
		}
	}
}

// TS_SMAPE(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSSMAPEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];

	UnifiedVectorFormat actual_format, predicted_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		auto smape = ::anofoxtime::utils::Metrics::smape(actual, predicted);
		if (smape.has_value()) {
			result_data[i] = smape.value();
		} else {
			result_validity.SetInvalid(i);
		}
	}
}

// TS_MASE(actual DOUBLE[], predicted DOUBLE[], baseline DOUBLE[]) -> DOUBLE
static void TSMASEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];
	auto &baseline_vec = args.data[2];

	UnifiedVectorFormat actual_format, predicted_format, baseline_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
	baseline_vec.ToUnifiedFormat(args.size(), baseline_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);
		auto baseline_idx = baseline_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx) ||
		    !baseline_format.validity.RowIsValid(baseline_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
		auto baseline = ExtractDoubleArray(baseline_vec, baseline_idx, baseline_format);

		if (actual.size() != predicted.size() || actual.size() != baseline.size()) {
			throw InvalidInputException("actual, predicted, and baseline arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		auto mase = ::anofoxtime::utils::Metrics::mase(actual, predicted, baseline);
		if (mase.has_value()) {
			result_data[i] = mase.value();
		} else {
			result_validity.SetInvalid(i);
		}
	}
}

// TS_R2(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSR2Function(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];

	UnifiedVectorFormat actual_format, predicted_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		auto r2 = ::anofoxtime::utils::Metrics::r2(actual, predicted);
		if (r2.has_value()) {
			result_data[i] = r2.value();
		} else {
			result_validity.SetInvalid(i);
		}
	}
}

// TS_BIAS(actual DOUBLE[], predicted DOUBLE[]) -> DOUBLE
static void TSBiasFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];

	UnifiedVectorFormat actual_format, predicted_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		// Use anofox-time bias calculation
		result_data[i] = ::anofoxtime::utils::Metrics::bias(actual, predicted);
	}
}

// TS_RMAE - Relative Mean Absolute Error (compares two forecasting methods)
static void TSRMAEFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	D_ASSERT(args.data.size() == 3);

	auto &actual_vec = args.data[0];
	auto &predicted1_vec = args.data[1];
	auto &predicted2_vec = args.data[2];

	UnifiedVectorFormat actual_format, predicted1_format, predicted2_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted1_vec.ToUnifiedFormat(args.size(), predicted1_format);
	predicted2_vec.ToUnifiedFormat(args.size(), predicted2_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted1_idx = predicted1_format.sel->get_index(i);
		auto predicted2_idx = predicted2_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted1_format.validity.RowIsValid(predicted1_idx) ||
		    !predicted2_format.validity.RowIsValid(predicted2_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted1 = ExtractDoubleArray(predicted1_vec, predicted1_idx, predicted1_format);
		auto predicted2 = ExtractDoubleArray(predicted2_vec, predicted2_idx, predicted2_format);

		if (actual.size() != predicted1.size() || actual.size() != predicted2.size()) {
			throw InvalidInputException("actual, predicted1, and predicted2 arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		try {
			// Use anofox-time RMAE calculation
			result_data[i] = ::anofoxtime::utils::Metrics::rmae(actual, predicted1, predicted2);
		} catch (const std::exception &e) {
			throw InvalidInputException("TS_RMAE error: %s", e.what());
		}
	}
}

// TS_QUANTILE_LOSS - Quantile Loss (Pinball Loss)
static void TSQuantileLossFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	D_ASSERT(args.data.size() == 3);

	auto &actual_vec = args.data[0];
	auto &predicted_vec = args.data[1];
	auto &quantile_vec = args.data[2];

	UnifiedVectorFormat actual_format, predicted_format, quantile_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_vec.ToUnifiedFormat(args.size(), predicted_format);
	quantile_vec.ToUnifiedFormat(args.size(), quantile_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);
	auto quantile_data = UnifiedVectorFormat::GetData<double>(quantile_format);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_idx = predicted_format.sel->get_index(i);
		auto quantile_idx = quantile_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !predicted_format.validity.RowIsValid(predicted_idx) ||
		    !quantile_format.validity.RowIsValid(quantile_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto predicted = ExtractDoubleArray(predicted_vec, predicted_idx, predicted_format);
		double q = quantile_data[quantile_idx];

		if (actual.size() != predicted.size()) {
			throw InvalidInputException("actual and predicted arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		try {
			// Use anofox-time quantile_loss calculation
			result_data[i] = ::anofoxtime::utils::Metrics::quantile_loss(actual, predicted, q);
		} catch (const std::exception &e) {
			throw InvalidInputException("TS_QUANTILE_LOSS error: %s", e.what());
		}
	}
}

// TS_COVERAGE - Prediction Interval Coverage
static void TSCoverageFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &actual_vec = args.data[0];
	auto &lower_vec = args.data[1];
	auto &upper_vec = args.data[2];

	UnifiedVectorFormat actual_format, lower_format, upper_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	lower_vec.ToUnifiedFormat(args.size(), lower_format);
	upper_vec.ToUnifiedFormat(args.size(), upper_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto lower_idx = lower_format.sel->get_index(i);
		auto upper_idx = upper_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) || !lower_format.validity.RowIsValid(lower_idx) ||
		    !upper_format.validity.RowIsValid(upper_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto lower = ExtractDoubleArray(lower_vec, lower_idx, lower_format);
		auto upper = ExtractDoubleArray(upper_vec, upper_idx, upper_format);

		if (actual.size() != lower.size() || actual.size() != upper.size()) {
			throw InvalidInputException("actual, lower, and upper arrays must have the same length");
		}
		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		result_data[i] = ::anofoxtime::utils::Metrics::coverage(actual, lower, upper);
	}
}

// TS_MQLOSS - Multi-Quantile Loss
static void TSMQLOSSFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	D_ASSERT(args.data.size() == 3);

	auto &actual_vec = args.data[0];
	auto &predicted_quantiles_vec = args.data[1]; // LIST of LISTs
	auto &quantiles_vec = args.data[2];

	UnifiedVectorFormat actual_format, predicted_quantiles_format, quantiles_format;
	actual_vec.ToUnifiedFormat(args.size(), actual_format);
	predicted_quantiles_vec.ToUnifiedFormat(args.size(), predicted_quantiles_format);
	quantiles_vec.ToUnifiedFormat(args.size(), quantiles_format);

	auto result_data = FlatVector::GetData<double>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < args.size(); i++) {
		auto actual_idx = actual_format.sel->get_index(i);
		auto predicted_quantiles_idx = predicted_quantiles_format.sel->get_index(i);
		auto quantiles_idx = quantiles_format.sel->get_index(i);

		if (!actual_format.validity.RowIsValid(actual_idx) ||
		    !predicted_quantiles_format.validity.RowIsValid(predicted_quantiles_idx) ||
		    !quantiles_format.validity.RowIsValid(quantiles_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto actual = ExtractDoubleArray(actual_vec, actual_idx, actual_format);
		auto quantiles = ExtractDoubleArray(quantiles_vec, quantiles_idx, quantiles_format);

		// Extract list of lists for predicted quantiles
		auto predicted_quantiles_list_value =
		    UnifiedVectorFormat::GetData<list_entry_t>(predicted_quantiles_format)[predicted_quantiles_idx];
		auto &predicted_quantiles_child = ListVector::GetEntry(predicted_quantiles_vec);

		std::vector<std::vector<double>> predicted_quantiles;
		for (idx_t q_idx = 0; q_idx < predicted_quantiles_list_value.length; ++q_idx) {
			idx_t child_idx = predicted_quantiles_list_value.offset + q_idx;
			UnifiedVectorFormat child_format;
			predicted_quantiles_child.ToUnifiedFormat(predicted_quantiles_list_value.length, child_format);
			auto pq = ExtractDoubleArray(predicted_quantiles_child, child_idx, child_format);
			predicted_quantiles.push_back(pq);
		}

		if (actual.empty()) {
			throw InvalidInputException("arrays must not be empty");
		}

		try {
			// Use anofox-time mqloss calculation
			result_data[i] = ::anofoxtime::utils::Metrics::mqloss(actual, predicted_quantiles, quantiles);
		} catch (const std::exception &e) {
			throw InvalidInputException("TS_MQLOSS error: %s", e.what());
		}
	}
}

void RegisterMetricsFunction(ExtensionLoader &loader) {
	// Helper to register with alias
	auto register_with_alias = [&loader](const string &full_name, const string &alias_name,
	                                     const vector<LogicalType> &args, LogicalType ret_type,
	                                     scalar_function_t func) {
		ScalarFunction main_func(full_name, args, ret_type, func);
		ScalarFunctionSet main_set(full_name);
		main_set.AddFunction(main_func);
		CreateScalarFunctionInfo main_info(std::move(main_set));
		main_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		loader.RegisterFunction(std::move(main_info));

		ScalarFunction alias_func(alias_name, args, ret_type, func);
		ScalarFunctionSet alias_set(alias_name);
		alias_set.AddFunction(alias_func);
		CreateScalarFunctionInfo alias_info(std::move(alias_set));
		alias_info.alias_of = full_name;
		alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		loader.RegisterFunction(std::move(alias_info));
	};

	// TS_MAE
	register_with_alias("anofox_fcst_ts_mae", "ts_mae",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSMAEFunction);

	// TS_MSE
	register_with_alias("anofox_fcst_ts_mse", "ts_mse",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSMSEFunction);

	// TS_RMSE
	register_with_alias("anofox_fcst_ts_rmse", "ts_rmse",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSRMSEFunction);

	// TS_MAPE
	register_with_alias("anofox_fcst_ts_mape", "ts_mape",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSMAPEFunction);

	// TS_SMAPE
	register_with_alias("anofox_fcst_ts_smape", "ts_smape",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSSMAPEFunction);

	// TS_MASE (requires baseline)
	register_with_alias("anofox_fcst_ts_mase", "ts_mase",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                     LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSMASEFunction);

	// TS_R2
	register_with_alias("anofox_fcst_ts_r2", "ts_r2",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSR2Function);

	// TS_BIAS
	register_with_alias("anofox_fcst_ts_bias", "ts_bias",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSBiasFunction);

	// TS_RMAE (Relative MAE - compares two forecasting methods)
	register_with_alias("anofox_fcst_ts_rmae", "ts_rmae",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                     LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSRMAEFunction);

	// TS_QUANTILE_LOSS (Pinball loss for quantile forecasts)
	register_with_alias(
	    "anofox_fcst_ts_quantile_loss", "ts_quantile_loss",
	    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
	    LogicalType::DOUBLE, TSQuantileLossFunction);

	// TS_MQLOSS (Multi-quantile loss for distribution forecasts)
	register_with_alias("anofox_fcst_ts_mqloss", "ts_mqloss",
	                    {LogicalType::LIST(LogicalType::DOUBLE),
	                     LogicalType::LIST(LogicalType::LIST(LogicalType::DOUBLE)),
	                     LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSMQLOSSFunction);

	// TS_COVERAGE (Prediction interval coverage)
	register_with_alias("anofox_fcst_ts_coverage", "ts_coverage",
	                    {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE),
	                     LogicalType::LIST(LogicalType::DOUBLE)},
	                    LogicalType::DOUBLE, TSCoverageFunction);
}

} // namespace duckdb
