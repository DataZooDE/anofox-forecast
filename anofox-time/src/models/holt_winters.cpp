#include "anofox-time/models/holt_winters.hpp"
#include "anofox-time/utils/logging.hpp"
#include <stdexcept>

namespace anofoxtime::models {

HoltWinters::HoltWinters(int seasonal_period, SeasonType season_type,
                         double alpha, double beta, double gamma)
    : seasonal_period_(seasonal_period), season_type_(season_type) {
	if (seasonal_period_ < 2) {
		throw std::invalid_argument("Seasonal period must be >= 2 for Holt-Winters");
	}
	
	// Configure ETS based on season type
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::Additive;
	config.season = (season_type_ == SeasonType::Additive) ? 
	                ETSSeasonType::Additive : 
	                ETSSeasonType::Multiplicative;
	config.season_length = seasonal_period_;
	config.alpha = alpha;
	config.beta = beta;
	config.gamma = gamma;
	
	ets_model_ = std::make_unique<ETS>(config);
}

void HoltWinters::fit(const core::TimeSeries& ts) {
	if (!ets_model_) {
		throw std::runtime_error("HoltWinters: ETS model not initialized");
	}
	
	ets_model_->fit(ts);
	is_fitted_ = true;
	
	const char* season_name = (season_type_ == SeasonType::Additive) ? "Additive" : "Multiplicative";
	ANOFOX_INFO("HoltWinters model fitted with {} seasonality, period={}", 
	            season_name, seasonal_period_);
}

core::Forecast HoltWinters::predict(int horizon) {
	if (!is_fitted_ || !ets_model_) {
		throw std::runtime_error("HoltWinters::predict called before fit");
	}
	
	return ets_model_->predict(horizon);
}

const std::vector<double>& HoltWinters::fittedValues() const {
	if (!ets_model_) {
		static const std::vector<double> empty;
		return empty;
	}
	return ets_model_->fittedValues();
}

const std::vector<double>& HoltWinters::residuals() const {
	if (!ets_model_) {
		static const std::vector<double> empty;
		return empty;
	}
	return ets_model_->residuals();
}

} // namespace anofoxtime::models

