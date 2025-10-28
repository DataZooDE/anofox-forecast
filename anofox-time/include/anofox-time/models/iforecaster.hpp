#pragma once

#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include "anofox-time/utils/metrics.hpp"
#include <optional>
#include <string>
#include <memory>
#include <vector>

namespace anofoxtime::models {

/**
 * @class IForecaster
 * @brief An interface for all forecasting models.
 *
 * This abstract base class defines the common structure for all time series
 * forecasting models in the library. It ensures a consistent API for fitting
 * models and generating predictions.
 */
class IForecaster {
public:
	virtual ~IForecaster() = default;

	/**
	 * @brief Fits the model to the provided time series data.
	 * @param ts The time series data to train the model on.
	 */
	virtual void fit(const core::TimeSeries &ts) = 0;

	/**
	 * @brief Generates forecasts for a specified number of steps into the future.
	 * @param horizon The number of future time steps to predict.
	 * @return A Forecast object containing the point predictions.
	 */
	virtual core::Forecast predict(int horizon) = 0;

	/**
	 * @brief Evaluates accuracy metrics against provided actual values.
	 * @param actual Series of ground-truth values.
	 * @param predicted Model predictions aligned to actuals.
	 * @param baseline Optional baseline predictions for MASE.
	 * @return Struct with standard accuracy metrics (MAE/MSE/RMSE/MAPE/SMAPE/MASE/R²).
	 */
	virtual utils::AccuracyMetrics score(const std::vector<double> &actual, const std::vector<double> &predicted,
	                                     const std::optional<std::vector<double>> &baseline = std::nullopt) const {
		utils::AccuracyMetrics metrics;
		metrics.n = actual.size();
		metrics.mae = utils::Metrics::mae(actual, predicted);
		metrics.mse = utils::Metrics::mse(actual, predicted);
		metrics.rmse = utils::Metrics::rmse(actual, predicted);
		metrics.mape = utils::Metrics::mape(actual, predicted);
		metrics.smape = utils::Metrics::smape(actual, predicted);
		if (baseline) {
			metrics.mase = utils::Metrics::mase(actual, predicted, *baseline);
		}
		metrics.r_squared = utils::Metrics::r2(actual, predicted);
		return metrics;
	}

	/**
	 * @brief Gets the name of the forecasting model.
	 * @return A string representing the model's name (e.g., "SimpleMovingAverage").
	 */
	virtual std::string getName() const = 0;
};

} // namespace anofoxtime::models
