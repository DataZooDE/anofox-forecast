#include "anofox-time/utils/cross_validation.hpp"
#include <stdexcept>
#include <algorithm>
#include <numeric>

namespace anofoxtime::utils {

std::vector<std::tuple<int, int, int, int>> CrossValidation::generateFolds(
	int n_samples,
	const CVConfig& config
) {
	if (n_samples < config.initial_window + config.horizon) {
		throw std::invalid_argument(
			"Time series too short for cross-validation. Need at least initial_window + horizon samples."
		);
	}

	std::vector<std::tuple<int, int, int, int>> folds;

	int pos = config.initial_window;

	while (pos + config.horizon <= n_samples) {
		int train_start, train_end;

		if (config.strategy == CVStrategy::EXPANDING) {
			// Expanding window: train from beginning to current position
			train_start = 0;
			train_end = pos;
		} else {
			// Rolling window: fixed-size window
			int window_size = (config.max_window > 0)
				? std::min(config.max_window, pos)
				: config.initial_window;
			train_start = pos - window_size;
			train_end = pos;
		}

		int test_start = pos;
		int test_end = std::min(pos + config.horizon, n_samples);

		folds.emplace_back(train_start, train_end, test_start, test_end);

		// Move to next fold
		pos += config.step;
	}

	return folds;
}

CVResults CrossValidation::evaluate(
	const core::TimeSeries& ts,
	std::function<std::unique_ptr<models::IForecaster>()> model_factory,
	const CVConfig& config
) {
	const auto& data = ts.getValues();
	const auto& timestamps = ts.getTimestamps();
	int n_samples = static_cast<int>(data.size());

	// Generate fold indices
	auto fold_indices = generateFolds(n_samples, config);

	if (fold_indices.empty()) {
		throw std::runtime_error("No CV folds generated. Check configuration.");
	}

	CVResults results;
	results.folds.reserve(fold_indices.size());

	// Run CV for each fold
	int fold_id = 0;
	for (const auto& [train_start, train_end, test_start, test_end] : fold_indices) {
		CVFold fold;
		fold.fold_id = fold_id++;
		fold.train_start = train_start;
		fold.train_end = train_end;
		fold.test_start = test_start;
		fold.test_end = test_end;

		// Extract training data
		std::vector<double> train_data(data.begin() + train_start, data.begin() + train_end);
		std::vector<core::TimeSeries::TimePoint> train_timestamps(
			timestamps.begin() + train_start,
			timestamps.begin() + train_end
		);

		core::TimeSeries train_ts(train_timestamps, train_data);

		// Create and fit model
		auto model = model_factory();
		try {
			model->fit(train_ts);

			// Forecast
			int h = test_end - test_start;
			auto forecast_result = model->predict(h);
			fold.forecasts = forecast_result.primary();

			// Extract actuals
			fold.actuals.assign(data.begin() + test_start, data.begin() + test_end);

			// Compute metrics for this fold
			if (!fold.forecasts.empty() && !fold.actuals.empty()) {
				fold.mae = Metrics::mae(fold.actuals, fold.forecasts);
				fold.mse = Metrics::mse(fold.actuals, fold.forecasts);
				fold.rmse = Metrics::rmse(fold.actuals, fold.forecasts);
				fold.mape = Metrics::mape(fold.actuals, fold.forecasts);
				fold.smape = Metrics::smape(fold.actuals, fold.forecasts);
			}
		} catch (const std::exception& e) {
			// Fold failed - skip it (could log warning here)
			// Set metrics to NaN to indicate failure
			fold.mae = std::numeric_limits<double>::quiet_NaN();
			fold.mse = std::numeric_limits<double>::quiet_NaN();
			fold.rmse = std::numeric_limits<double>::quiet_NaN();
		}

		results.folds.push_back(std::move(fold));
	}

	// Compute aggregated metrics
	results.computeAggregatedMetrics();

	return results;
}

void CVResults::computeAggregatedMetrics() {
	// Collect all forecasts and actuals from all folds
	std::vector<double> all_forecasts;
	std::vector<double> all_actuals;

	for (const auto& fold : folds) {
		// Skip failed folds
		if (std::isnan(fold.mae)) {
			continue;
		}

		all_forecasts.insert(all_forecasts.end(), fold.forecasts.begin(), fold.forecasts.end());
		all_actuals.insert(all_actuals.end(), fold.actuals.begin(), fold.actuals.end());
	}

	total_forecasts = static_cast<int>(all_forecasts.size());

	if (total_forecasts == 0) {
		// No successful folds
		mae = std::numeric_limits<double>::quiet_NaN();
		mse = std::numeric_limits<double>::quiet_NaN();
		rmse = std::numeric_limits<double>::quiet_NaN();
		return;
	}

	// Compute aggregated metrics across all forecasts
	mae = Metrics::mae(all_actuals, all_forecasts);
	mse = Metrics::mse(all_actuals, all_forecasts);
	rmse = Metrics::rmse(all_actuals, all_forecasts);
	mape = Metrics::mape(all_actuals, all_forecasts);
	smape = Metrics::smape(all_actuals, all_forecasts);
}

} // namespace anofoxtime::utils
