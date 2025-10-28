#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include "anofox-time/utils/metrics.hpp"
#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <optional>

namespace anofoxtime::models {

/**
 * @enum EnsembleCombinationMethod
 * @brief Specifies how to combine predictions from multiple forecasters
 */
enum class EnsembleCombinationMethod {
	/**
	 * @brief Simple arithmetic mean of all forecasts (equal weights)
	 */
	Mean,
	
	/**
	 * @brief Median of all forecasts (robust to outliers)
	 */
	Median,
	
	/**
	 * @brief Weighted average based on AIC (lower AIC = higher weight)
	 * Models without AIC are excluded from the ensemble
	 */
	WeightedAIC,
	
	/**
	 * @brief Weighted average based on BIC (lower BIC = higher weight)
	 * Models without BIC are excluded from the ensemble
	 */
	WeightedBIC,
	
	/**
	 * @brief Weighted average based on forecast accuracy metric
	 * Requires validation data or uses in-sample accuracy
	 */
	WeightedAccuracy
};

/**
 * @enum AccuracyMetric
 * @brief Metric to use for accuracy-weighted ensembles
 */
enum class AccuracyMetric {
	MAE,   ///< Mean Absolute Error (lower is better)
	MSE,   ///< Mean Squared Error (lower is better)
	RMSE,  ///< Root Mean Squared Error (lower is better)
	MAPE,  ///< Mean Absolute Percentage Error (lower is better)
	SMAPE  ///< Symmetric Mean Absolute Percentage Error (lower is better)
};

/**
 * @struct EnsembleConfig
 * @brief Configuration for ensemble forecaster behavior
 */
struct EnsembleConfig {
	/// Method for combining forecasts
	EnsembleCombinationMethod method = EnsembleCombinationMethod::Mean;
	
	/// Metric to use for accuracy-weighted ensembles
	AccuracyMetric accuracy_metric = AccuracyMetric::MAE;
	
	/// Validation split ratio (0 to 1) for computing accuracy weights
	/// If 0, uses in-sample accuracy; if > 0, holds out this fraction for validation
	double validation_split = 0.2;
	
	/// Minimum weight threshold (weights below this are set to 0)
	double min_weight = 0.0;
	
	/// Whether to normalize weights to sum to 1.0
	bool normalize_weights = true;
	
	/// Temperature parameter for softmax weighting (only for IC/accuracy weighting)
	/// Higher values = more uniform weights, lower values = more extreme weights
	double temperature = 1.0;
};

/**
 * @class Ensemble
 * @brief Combines multiple forecasters into a single ensemble model
 * 
 * This class implements ensemble forecasting by combining predictions from
 * multiple base forecasters. It supports various combination methods including
 * simple averaging, median, and sophisticated weighting schemes based on
 * information criteria (AIC/BIC) or forecast accuracy.
 * 
 * The ensemble itself implements the IForecaster interface, making it compatible
 * with all library features including backtesting and model selection.
 * 
 * @example
 * ```cpp
 * // Create base forecasters
 * std::vector<std::shared_ptr<IForecaster>> models;
 * models.push_back(std::make_shared<Naive>());
 * models.push_back(std::make_shared<SES>());
 * models.push_back(std::make_shared<Theta>());
 * 
 * // Create ensemble with mean combination
 * EnsembleConfig config;
 * config.method = EnsembleCombinationMethod::Mean;
 * Ensemble ensemble(models, config);
 * 
 * // Use like any other forecaster
 * ensemble.fit(ts);
 * auto forecast = ensemble.predict(12);
 * ```
 */
class Ensemble : public IForecaster {
public:
	/**
	 * @brief Constructs an ensemble from a vector of forecasters
	 * @param forecasters Shared pointers to base forecasters
	 * @param config Ensemble configuration
	 * @throws std::invalid_argument if forecasters vector is empty
	 */
	explicit Ensemble(
		std::vector<std::shared_ptr<IForecaster>> forecasters,
		const EnsembleConfig& config = EnsembleConfig{}
	);
	
	/**
	 * @brief Constructs an ensemble from a vector of forecaster factories
	 * 
	 * This constructor is useful when you need fresh instances for each fit.
	 * Each factory should return a new forecaster instance.
	 * 
	 * @param factories Functions that create new forecaster instances
	 * @param config Ensemble configuration
	 * @throws std::invalid_argument if factories vector is empty
	 */
	explicit Ensemble(
		std::vector<std::function<std::shared_ptr<IForecaster>()>> factories,
		const EnsembleConfig& config = EnsembleConfig{}
	);
	
	/**
	 * @brief Fits all base forecasters to the time series
	 * 
	 * For accuracy-weighted ensembles, this splits the data according to
	 * validation_split to compute accuracy weights.
	 * 
	 * @param ts The time series data to train on
	 */
	void fit(const core::TimeSeries& ts) override;
	
	/**
	 * @brief Generates ensemble forecast by combining base forecaster predictions
	 * @param horizon Number of future time steps to predict
	 * @return Combined forecast
	 */
	core::Forecast predict(int horizon) override;
	
	/**
	 * @brief Gets the name of the ensemble
	 * @return String describing the ensemble configuration
	 */
	std::string getName() const override;
	
	/**
	 * @brief Gets the current weights assigned to each forecaster
	 * 
	 * For mean ensemble, all weights are equal (1/n).
	 * For median ensemble, weights are not applicable (returns empty vector).
	 * For weighted ensembles, returns the computed weights.
	 * 
	 * @return Vector of weights (same order as input forecasters)
	 */
	std::vector<double> getWeights() const {
		return weights_;
	}
	
	/**
	 * @brief Gets the base forecasters
	 * @return Vector of shared pointers to base forecasters
	 */
	const std::vector<std::shared_ptr<IForecaster>>& getForecasters() const {
		return forecasters_;
	}
	
	/**
	 * @brief Gets individual forecasts from each base forecaster
	 * 
	 * Useful for diagnostic purposes and understanding ensemble composition.
	 * 
	 * @param horizon Number of future time steps to predict
	 * @return Vector of forecasts (same order as input forecasters)
	 */
	std::vector<core::Forecast> getIndividualForecasts(int horizon) const;
	
	/**
	 * @brief Gets the ensemble configuration
	 * @return Current configuration
	 */
	const EnsembleConfig& getConfig() const {
		return config_;
	}
	
	/**
	 * @brief Updates the ensemble configuration
	 * 
	 * Note: Changing configuration after fitting requires refitting to recompute weights.
	 * 
	 * @param config New configuration
	 */
	void setConfig(const EnsembleConfig& config) {
		config_ = config;
		is_fitted_ = false; // Need to refit with new config
	}

private:
	/// Base forecasters
	std::vector<std::shared_ptr<IForecaster>> forecasters_;
	
	/// Forecaster factories (for creating fresh instances)
	std::vector<std::function<std::shared_ptr<IForecaster>()>> factories_;
	
	/// Ensemble configuration
	EnsembleConfig config_;
	
	/// Computed weights for each forecaster
	std::vector<double> weights_;
	
	/// Whether the ensemble has been fitted
	bool is_fitted_ = false;
	
	/// Whether we're using factories
	bool use_factories_ = false;
	
	/**
	 * @brief Combines forecasts using the configured method
	 * @param forecasts Individual forecasts from base models
	 * @return Combined forecast
	 */
	core::Forecast combineForecasts(const std::vector<core::Forecast>& forecasts) const;
	
	/**
	 * @brief Computes weights based on the configured method
	 * @param ts Training time series (for accuracy-based weighting)
	 */
	void computeWeights(const core::TimeSeries& ts);
	
	/**
	 * @brief Computes equal weights for mean ensemble
	 */
	void computeMeanWeights();
	
	/**
	 * @brief Computes weights based on AIC
	 */
	void computeAICWeights();
	
	/**
	 * @brief Computes weights based on BIC
	 */
	void computeBICWeights();
	
	/**
	 * @brief Computes weights based on forecast accuracy
	 * @param ts Training time series
	 */
	void computeAccuracyWeights(const core::TimeSeries& ts);
	
	/**
	 * @brief Applies softmax transformation to convert scores to weights
	 * @param scores Vector of scores (lower is better)
	 * @return Normalized weights
	 */
	std::vector<double> softmaxWeights(const std::vector<double>& scores) const;
	
	/**
	 * @brief Normalizes weights to sum to 1.0
	 * @param weights Weights to normalize (modified in place)
	 */
	void normalizeWeights(std::vector<double>& weights) const;
	
	/**
	 * @brief Applies minimum weight threshold
	 * @param weights Weights to threshold (modified in place)
	 */
	void applyMinWeightThreshold(std::vector<double>& weights) const;
	
	/**
	 * @brief Extracts metric value from AccuracyMetrics struct
	 * @param metrics Computed accuracy metrics
	 * @return Value of the configured metric
	 */
	double extractMetricValue(const utils::AccuracyMetrics& metrics) const;
	
	/**
	 * @brief Gets AIC value from a forecaster if available
	 * @param forecaster Forecaster to query
	 * @return AIC value or nullopt if not available
	 */
	std::optional<double> getAIC(const IForecaster& forecaster) const;
	
	/**
	 * @brief Gets BIC value from a forecaster if available
	 * @param forecaster Forecaster to query
	 * @return BIC value or nullopt if not available
	 */
	std::optional<double> getBIC(const IForecaster& forecaster) const;
};

} // namespace anofoxtime::models

