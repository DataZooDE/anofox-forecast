#pragma once

#include "anofox-time/core/time_series.hpp"
#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/metrics.hpp"
#include <vector>
#include <functional>
#include <memory>

namespace anofoxtime::utils {

/**
 * @brief Time series cross-validation strategy
 */
enum class CVStrategy {
	ROLLING,   // Fixed-size rolling window
	EXPANDING  // Expanding window (cumulative)
};

/**
 * @brief Optimization metric for cross-validation
 */
enum class CVMetric {
	MAE,    // Mean Absolute Error
	RMSE,   // Root Mean Squared Error  
	MAPE,   // Mean Absolute Percentage Error
	SMAPE   // Symmetric Mean Absolute Percentage Error
};

/**
 * @brief Configuration for time series cross-validation
 */
struct CVConfig {
	int horizon = 1;              // Forecast horizon
	int initial_window = 50;      // Initial training window size
	int step = 1;                 // Step size between folds
	CVStrategy strategy = CVStrategy::ROLLING;

	// For rolling window: maximum window size (0 = use initial_window)
	int max_window = 0;
};

/**
 * @brief Results from a single CV fold
 */
struct CVFold {
	int fold_id = 0;              // Fold number
	int train_start = 0;          // Start index of training data
	int train_end = 0;            // End index of training data (exclusive)
	int test_start = 0;           // Start index of test data
	int test_end = 0;             // End index of test data (exclusive)

	std::vector<double> forecasts; // Forecast values
	std::vector<double> actuals;   // Actual values

	// Metrics for this fold
	double mae = 0.0;
	double mse = 0.0;
	double rmse = 0.0;
	std::optional<double> mape;
	std::optional<double> smape;
};

/**
 * @brief Results from cross-validation
 */
struct CVResults {
	std::vector<CVFold> folds;

	// Aggregated metrics across all folds
	double mae = 0.0;
	double mse = 0.0;
	double rmse = 0.0;
	std::optional<double> mape;
	std::optional<double> smape;

	int total_forecasts = 0;

	/**
	 * @brief Compute aggregated metrics from all folds
	 */
	void computeAggregatedMetrics();

	/**
	 * @brief Get the value of a specific metric
	 * @param metric The metric to retrieve
	 * @return The metric value
	 */
	double getMetric(CVMetric metric) const;
};

/**
 * @brief Time series cross-validation utility
 */
class CrossValidation {
public:
	/**
	 * @brief Perform cross-validation on a time series
	 *
	 * @param ts Time series to validate
	 * @param model_factory Function that creates a new model instance for each fold
	 * @param config CV configuration
	 * @return CVResults with fold-wise and aggregated metrics
	 */
	static CVResults evaluate(
		const core::TimeSeries& ts,
		std::function<std::unique_ptr<models::IForecaster>()> model_factory,
		const CVConfig& config = CVConfig{}
	);

	/**
	 * @brief Generate CV fold indices
	 *
	 * @param n_samples Total number of samples
	 * @param config CV configuration
	 * @return Vector of (train_start, train_end, test_start, test_end) tuples
	 */
	static std::vector<std::tuple<int, int, int, int>> generateFolds(
		int n_samples,
		const CVConfig& config
	);
};

} // namespace anofoxtime::utils
