#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/logging.hpp"
#include <optional>
#include <vector>

namespace anofoxtime::models {

enum class ETSErrorType {
	Additive,
	Multiplicative // Reserved for future support
};

enum class ETSTrendType {
	None,
	Additive,
	Multiplicative,
	DampedAdditive,
	DampedMultiplicative
};

enum class ETSSeasonType {
	None,
	Additive,
	Multiplicative
};

struct ETSConfig {
	ETSErrorType error = ETSErrorType::Additive;
	ETSTrendType trend = ETSTrendType::None;
	ETSSeasonType season = ETSSeasonType::None;
	int season_length = 0;
	double alpha = 0.2;
	std::optional<double> beta;
	std::optional<double> gamma;
	double phi = 0.98;
};

class ETS : public IForecaster {
public:
	explicit ETS(ETSConfig config);

	void fit(const core::TimeSeries &ts) override;
	void fitRaw(const std::vector<double> &values);
	void fitWithInitialState(const std::vector<double> &values,
	                         std::optional<double> level0,
	                         std::optional<double> trend0);
	void fitWithFullState(const std::vector<double> &values,
	                      std::optional<double> level0,
	                      std::optional<double> trend0,
	                      const std::vector<double> &seasonal0);
	core::Forecast predict(int horizon) override;

	std::string getName() const override {
		return "ETS";
	}

	const std::vector<double> &fittedValues() const {
		return fitted_;
	}

	const std::vector<double> &residuals() const {
		return residuals_;
	}

	const ETSConfig &config() const {
		return config_;
	}

	double logLikelihood() const {
		return log_likelihood_;
	}

	double mse() const {
		return mse_;
	}

	double sse() const {
		return sse_;
	}

	double innovationSSE() const {
		return innovation_sse_;
	}

	std::size_t sampleSize() const {
		return sample_size_;
	}

	double aic(std::size_t parameter_count) const;
	double aicc(std::size_t parameter_count) const;

	double lastLevel() const {
		return level_;
	}

	double lastTrend() const {
		return trend_;
	}

private:
	ETSConfig config_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	std::vector<double> seasonals_;
	std::size_t last_season_index_ = 0;
	double level_ = 0.0;
	double trend_ = 0.0;
	bool is_fitted_ = false;
	double log_likelihood_ = 0.0;
	double mse_ = 0.0;
	double sse_ = 0.0;
	double innovation_sse_ = 0.0;
	std::size_t sample_size_ = 0;
	double sum_log_forecast_ = 0.0;

	void initializeStates(const std::vector<double> &values);
	void validateConfig() const;
	void fitInternal(const std::vector<double> &values,
	                 std::optional<double> level_override,
	                 std::optional<double> trend_override,
	                 const std::vector<double> *seasonal_override = nullptr);
	double computeForecastComponent(double horizon_step) const;
};

class ETSBuilder {
public:
	ETSBuilder &withError(ETSErrorType error) {
		config_.error = error;
		return *this;
	}

	ETSBuilder &withTrend(ETSTrendType trend) {
		config_.trend = trend;
		return *this;
	}

	ETSBuilder &withSeason(ETSSeasonType season, int season_length = 0) {
		config_.season = season;
		config_.season_length = season_length;
		return *this;
	}

	ETSBuilder &withAlpha(double alpha) {
		config_.alpha = alpha;
		return *this;
	}

	ETSBuilder &withBeta(double beta) {
		config_.beta = beta;
		return *this;
	}

	ETSBuilder &withGamma(double gamma) {
		config_.gamma = gamma;
		return *this;
	}

	ETSBuilder &withPhi(double phi) {
		config_.phi = phi;
		return *this;
	}

	ETSBuilder &withSeasonLength(int season_length) {
		config_.season_length = season_length;
		return *this;
	}

	ETSConfig config() const {
		return config_;
	}

	std::unique_ptr<ETS> build() const {
		return std::unique_ptr<ETS>(new ETS(config_));
	}

private:
	ETSConfig config_;
};

} // namespace anofoxtime::models
