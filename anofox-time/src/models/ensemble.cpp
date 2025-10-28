#include "anofox-time/models/ensemble.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/models/tbats.hpp"
#include "anofox-time/utils/logging.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace anofoxtime::models {

Ensemble::Ensemble(
	std::vector<std::shared_ptr<IForecaster>> forecasters,
	const EnsembleConfig& config
) : forecasters_(std::move(forecasters)), config_(config), use_factories_(false) {
	if (forecasters_.empty()) {
		throw std::invalid_argument("Ensemble: At least one forecaster is required");
	}
	
	// Initialize weights vector
	weights_.resize(forecasters_.size(), 0.0);
}

Ensemble::Ensemble(
	std::vector<std::function<std::shared_ptr<IForecaster>()>> factories,
	const EnsembleConfig& config
) : factories_(std::move(factories)), config_(config), use_factories_(true) {
	if (factories_.empty()) {
		throw std::invalid_argument("Ensemble: At least one forecaster factory is required");
	}
	
	// Create initial instances from factories
	forecasters_.reserve(factories_.size());
	for (const auto& factory : factories_) {
		forecasters_.push_back(factory());
	}
	
	// Initialize weights vector
	weights_.resize(forecasters_.size(), 0.0);
}

void Ensemble::fit(const core::TimeSeries& ts) {
	if (ts.isEmpty()) {
		throw std::invalid_argument("Ensemble::fit: Cannot fit to empty time series");
	}
	
	// If using factories, create fresh instances
	if (use_factories_) {
		forecasters_.clear();
		forecasters_.reserve(factories_.size());
		for (const auto& factory : factories_) {
			forecasters_.push_back(factory());
		}
		weights_.resize(forecasters_.size(), 0.0);
	}
	
	// For accuracy-weighted ensembles, we need to split the data
	core::TimeSeries train_ts = ts;
	std::optional<core::TimeSeries> validation_ts;
	
	if (config_.method == EnsembleCombinationMethod::WeightedAccuracy && 
	    config_.validation_split > 0.0 && config_.validation_split < 1.0) {
		
		const std::size_t n = ts.size();
		const std::size_t train_size = static_cast<std::size_t>(
			n * (1.0 - config_.validation_split)
		);
		
		if (train_size < 1 || train_size >= n) {
			throw std::invalid_argument("Ensemble: Invalid validation_split, results in empty train or validation set");
		}
		
		train_ts = ts.slice(0, train_size);
		validation_ts = ts.slice(train_size, n);
	}
	
	// Fit all base forecasters
	ANOFOX_INFO("Ensemble: Fitting {} base forecasters", forecasters_.size());
	
	for (std::size_t i = 0; i < forecasters_.size(); ++i) {
		try {
			forecasters_[i]->fit(train_ts);
		} catch (const std::exception& e) {
			ANOFOX_WARN("Ensemble: Forecaster {} ({}) failed to fit: {}", 
			           i, forecasters_[i]->getName(), e.what());
			// Continue with other forecasters
		}
	}
	
	// Compute weights based on the configured method
	computeWeights(train_ts);
	
	is_fitted_ = true;
	
	ANOFOX_INFO("Ensemble: Fitting complete");
}

core::Forecast Ensemble::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("Ensemble::predict: Must call fit() before predict()");
	}
	
	if (horizon <= 0) {
		throw std::invalid_argument("Ensemble::predict: horizon must be positive");
	}
	
	// Get predictions from all forecasters
	std::vector<core::Forecast> forecasts = getIndividualForecasts(horizon);
	
	// Combine forecasts
	return combineForecasts(forecasts);
}

std::string Ensemble::getName() const {
	std::string method_name;
	switch (config_.method) {
		case EnsembleCombinationMethod::Mean:
			method_name = "Mean";
			break;
		case EnsembleCombinationMethod::Median:
			method_name = "Median";
			break;
		case EnsembleCombinationMethod::WeightedAIC:
			method_name = "WeightedAIC";
			break;
		case EnsembleCombinationMethod::WeightedBIC:
			method_name = "WeightedBIC";
			break;
		case EnsembleCombinationMethod::WeightedAccuracy:
			method_name = "WeightedAccuracy";
			break;
	}
	
	return "Ensemble<" + method_name + ">[" + std::to_string(forecasters_.size()) + "]";
}

std::vector<core::Forecast> Ensemble::getIndividualForecasts(int horizon) const {
	std::vector<core::Forecast> forecasts;
	forecasts.reserve(forecasters_.size());
	
	for (const auto& forecaster : forecasters_) {
		try {
			forecasts.push_back(forecaster->predict(horizon));
		} catch (const std::exception& e) {
			ANOFOX_WARN("Ensemble: Forecaster {} failed to predict: {}", 
			           forecaster->getName(), e.what());
			// Create empty forecast as placeholder
			core::Forecast empty;
			empty.primary().resize(horizon, std::numeric_limits<double>::quiet_NaN());
			forecasts.push_back(std::move(empty));
		}
	}
	
	return forecasts;
}

core::Forecast Ensemble::combineForecasts(const std::vector<core::Forecast>& forecasts) const {
	if (forecasts.empty()) {
		throw std::runtime_error("Ensemble::combineForecasts: No forecasts to combine");
	}
	
	const int horizon = static_cast<int>(forecasts[0].horizon());
	
	// Check all forecasts have the same horizon
	for (const auto& forecast : forecasts) {
		if (static_cast<int>(forecast.horizon()) != horizon) {
			throw std::runtime_error("Ensemble::combineForecasts: All forecasts must have the same horizon");
		}
	}
	
	core::Forecast result;
	result.primary().resize(horizon, 0.0);
	
	switch (config_.method) {
		case EnsembleCombinationMethod::Mean: {
			// Simple arithmetic mean
			for (int h = 0; h < horizon; ++h) {
				double sum = 0.0;
				int count = 0;
				for (const auto& forecast : forecasts) {
					const double val = forecast.primary()[h];
					if (std::isfinite(val)) {
						sum += val;
						++count;
					}
				}
				result.primary()[h] = count > 0 ? sum / count : std::numeric_limits<double>::quiet_NaN();
			}
			break;
		}
		
		case EnsembleCombinationMethod::Median: {
			// Median across forecasts
			std::vector<double> values;
			values.reserve(forecasts.size());
			
			for (int h = 0; h < horizon; ++h) {
				values.clear();
				for (const auto& forecast : forecasts) {
					const double val = forecast.primary()[h];
					if (std::isfinite(val)) {
						values.push_back(val);
					}
				}
				
				if (values.empty()) {
					result.primary()[h] = std::numeric_limits<double>::quiet_NaN();
				} else {
					// Compute median
					std::nth_element(values.begin(), 
					                values.begin() + values.size() / 2, 
					                values.end());
					
					if (values.size() % 2 == 0) {
						const double median1 = values[values.size() / 2 - 1];
						const double median2 = values[values.size() / 2];
						result.primary()[h] = (median1 + median2) / 2.0;
					} else {
						result.primary()[h] = values[values.size() / 2];
					}
				}
			}
			break;
		}
		
		case EnsembleCombinationMethod::WeightedAIC:
		case EnsembleCombinationMethod::WeightedBIC:
		case EnsembleCombinationMethod::WeightedAccuracy: {
			// Weighted average
			for (int h = 0; h < horizon; ++h) {
				double weighted_sum = 0.0;
				double weight_sum = 0.0;
				
				for (std::size_t i = 0; i < forecasts.size(); ++i) {
					const double val = forecasts[i].primary()[h];
					const double weight = weights_[i];
					
					if (std::isfinite(val) && std::isfinite(weight) && weight > 0.0) {
						weighted_sum += weight * val;
						weight_sum += weight;
					}
				}
				
				result.primary()[h] = weight_sum > 0.0 ? 
					weighted_sum / weight_sum : 
					std::numeric_limits<double>::quiet_NaN();
			}
			break;
		}
	}
	
	return result;
}

void Ensemble::computeWeights(const core::TimeSeries& ts) {
	switch (config_.method) {
		case EnsembleCombinationMethod::Mean:
			computeMeanWeights();
			break;
		case EnsembleCombinationMethod::Median:
			// Median doesn't use weights
			weights_.assign(forecasters_.size(), 1.0);
			break;
		case EnsembleCombinationMethod::WeightedAIC:
			computeAICWeights();
			break;
		case EnsembleCombinationMethod::WeightedBIC:
			computeBICWeights();
			break;
		case EnsembleCombinationMethod::WeightedAccuracy:
			computeAccuracyWeights(ts);
			break;
	}
	
	// Apply min weight threshold if configured
	if (config_.min_weight > 0.0) {
		applyMinWeightThreshold(weights_);
	}
	
	// Normalize weights if configured
	if (config_.normalize_weights && 
	    config_.method != EnsembleCombinationMethod::Median) {
		normalizeWeights(weights_);
	}
}

void Ensemble::computeMeanWeights() {
	const double equal_weight = 1.0 / forecasters_.size();
	weights_.assign(forecasters_.size(), equal_weight);
}

void Ensemble::computeAICWeights() {
	std::vector<double> aics;
	aics.reserve(forecasters_.size());
	
	// Collect AIC values
	for (const auto& forecaster : forecasters_) {
		auto aic = getAIC(*forecaster);
		if (aic.has_value()) {
			aics.push_back(*aic);
		} else {
			aics.push_back(std::numeric_limits<double>::infinity());
		}
	}
	
	// Convert AIC to weights using softmax
	weights_ = softmaxWeights(aics);
	
	ANOFOX_DEBUG("Ensemble: Computed AIC weights");
}

void Ensemble::computeBICWeights() {
	std::vector<double> bics;
	bics.reserve(forecasters_.size());
	
	// Collect BIC values
	for (const auto& forecaster : forecasters_) {
		auto bic = getBIC(*forecaster);
		if (bic.has_value()) {
			bics.push_back(*bic);
		} else {
			bics.push_back(std::numeric_limits<double>::infinity());
		}
	}
	
	// Convert BIC to weights using softmax
	weights_ = softmaxWeights(bics);
	
	ANOFOX_DEBUG("Ensemble: Computed BIC weights");
}

void Ensemble::computeAccuracyWeights(const core::TimeSeries& ts) {
	const std::size_t n = ts.size();
	
	// Determine train/validation split
	std::size_t train_size = n;
	std::size_t validation_size = 0;
	
	if (config_.validation_split > 0.0 && config_.validation_split < 1.0) {
		train_size = static_cast<std::size_t>(n * (1.0 - config_.validation_split));
		validation_size = n - train_size;
	} else {
		// Use in-sample accuracy (on training data)
		train_size = n;
	}
	
	std::vector<double> errors;
	errors.reserve(forecasters_.size());
	
	if (validation_size > 0) {
		// Out-of-sample accuracy
		const auto train = ts.slice(0, train_size);
		const auto validation = ts.slice(train_size, n);
		const auto& actual = validation.getValues();
		
		for (const auto& forecaster : forecasters_) {
			try {
				// Forecaster is already fitted on training data
				auto forecast = forecaster->predict(static_cast<int>(validation_size));
				const auto& predicted = forecast.primary();
				
				// Compute accuracy metric
				auto metrics = forecaster->score(actual, predicted);
				double error = extractMetricValue(metrics);
				errors.push_back(error);
				
			} catch (const std::exception& e) {
				ANOFOX_WARN("Ensemble: Failed to compute accuracy for {}: {}", 
				           forecaster->getName(), e.what());
				errors.push_back(std::numeric_limits<double>::infinity());
			}
		}
	} else {
		// In-sample accuracy (use entire dataset)
		const auto& actual = ts.getValues();
		
		for (const auto& forecaster : forecasters_) {
			try {
				// Predict on the training data
				auto forecast = forecaster->predict(static_cast<int>(n));
				const auto& predicted = forecast.primary();
				
				// Resize predicted to match actual length (in case of different lengths)
				std::vector<double> predicted_vec(predicted.begin(), predicted.end());
				if (predicted_vec.size() > actual.size()) {
					predicted_vec.resize(actual.size());
				}
				
				// Compute accuracy metric
				auto metrics = forecaster->score(actual, predicted_vec);
				double error = extractMetricValue(metrics);
				errors.push_back(error);
				
			} catch (const std::exception& e) {
				ANOFOX_WARN("Ensemble: Failed to compute in-sample accuracy for {}: {}", 
				           forecaster->getName(), e.what());
				errors.push_back(std::numeric_limits<double>::infinity());
			}
		}
	}
	
	// Convert errors to weights using softmax
	weights_ = softmaxWeights(errors);
	
	ANOFOX_DEBUG("Ensemble: Computed accuracy-based weights");
}

std::vector<double> Ensemble::softmaxWeights(const std::vector<double>& scores) const {
	std::vector<double> weights(scores.size(), 0.0);
	
	// Find minimum score (lower is better)
	const double min_score = *std::min_element(scores.begin(), scores.end());
	
	// If all scores are infinite, use equal weights
	if (std::isinf(min_score)) {
		const double equal_weight = 1.0 / scores.size();
		std::fill(weights.begin(), weights.end(), equal_weight);
		return weights;
	}
	
	// Convert scores to weights using inverse softmax
	// w_i = exp(-(s_i - s_min) / T) where T is temperature
	double sum_exp = 0.0;
	for (std::size_t i = 0; i < scores.size(); ++i) {
		if (std::isfinite(scores[i])) {
			const double normalized_score = -(scores[i] - min_score) / config_.temperature;
			weights[i] = std::exp(normalized_score);
			sum_exp += weights[i];
		}
	}
	
	// Normalize
	if (sum_exp > 0.0) {
		for (auto& w : weights) {
			w /= sum_exp;
		}
	} else {
		// Fallback to equal weights
		const double equal_weight = 1.0 / scores.size();
		std::fill(weights.begin(), weights.end(), equal_weight);
	}
	
	return weights;
}

void Ensemble::normalizeWeights(std::vector<double>& weights) const {
	const double sum = std::accumulate(weights.begin(), weights.end(), 0.0);
	
	if (sum > 0.0 && std::isfinite(sum)) {
		for (auto& w : weights) {
			w /= sum;
		}
	} else {
		// If sum is 0 or invalid, use equal weights
		const double equal_weight = 1.0 / weights.size();
		std::fill(weights.begin(), weights.end(), equal_weight);
	}
}

void Ensemble::applyMinWeightThreshold(std::vector<double>& weights) const {
	for (auto& w : weights) {
		if (w < config_.min_weight) {
			w = 0.0;
		}
	}
}

double Ensemble::extractMetricValue(const utils::AccuracyMetrics& metrics) const {
	switch (config_.accuracy_metric) {
		case AccuracyMetric::MAE:
			return metrics.mae;
		case AccuracyMetric::MSE:
			return metrics.mse;
		case AccuracyMetric::RMSE:
			return metrics.rmse;
		case AccuracyMetric::MAPE:
			return metrics.mape.value_or(std::numeric_limits<double>::infinity());
		case AccuracyMetric::SMAPE:
			return metrics.smape.value_or(std::numeric_limits<double>::infinity());
		default:
			return metrics.mae;
	}
}

std::optional<double> Ensemble::getAIC(const IForecaster& forecaster) const {
	// Try to get AIC from known model types
	
	// ARIMA has AIC
	if (const auto* arima = dynamic_cast<const ARIMA*>(&forecaster)) {
		return arima->aic();
	}
	
	// TBATS has AIC
	if (const auto* tbats = dynamic_cast<const TBATS*>(&forecaster)) {
		try {
			return tbats->aic();
		} catch (...) {
			return std::nullopt;
		}
	}
	
	// For other models, we don't have AIC
	return std::nullopt;
}

std::optional<double> Ensemble::getBIC(const IForecaster& forecaster) const {
	// Try to get BIC from known model types
	
	// ARIMA has BIC
	if (const auto* arima = dynamic_cast<const ARIMA*>(&forecaster)) {
		return arima->bic();
	}
	
	// For other models, we don't have BIC
	return std::nullopt;
}

} // namespace anofoxtime::models

