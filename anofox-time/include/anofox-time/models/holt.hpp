#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/logging.hpp"
#include <stdexcept>

namespace anofoxtime::models {

class HoltLinearTrendBuilder; // Forward declaration

/**
 * @class HoltLinearTrend
 * @brief A forecasting model that extends Simple Exponential Smoothing to capture a trend.
 *
 * This model includes a second smoothing parameter, beta, for the trend component.
 */
class HoltLinearTrend final : public IForecaster {
public:
	friend class HoltLinearTrendBuilder;

	void fit(const core::TimeSeries &ts) override;
	core::Forecast predict(int horizon) override;
	std::string getName() const override {
		return "HoltLinearTrend";
	}

private:
	/**
	 * @brief Private constructor for HoltLinearTrend model.
	 * @param alpha The smoothing parameter for the level, between 0 and 1.
	 * @param beta The smoothing parameter for the trend, between 0 and 1.
	 */
	explicit HoltLinearTrend(double alpha, double beta);

	double alpha_;
	double beta_;
	double last_level_ = 0.0;
	double last_trend_ = 0.0;
	bool is_fitted_ = false;
};

/**
 * @class HoltLinearTrendBuilder
 * @brief A builder for fluently configuring and creating HoltLinearTrend models.
 */
class HoltLinearTrendBuilder {
public:
	/**
	 * @brief Sets the alpha smoothing parameter for the level.
	 * @param alpha The smoothing parameter (0 to 1).
	 * @return A reference to the builder for chaining.
	 */
	HoltLinearTrendBuilder &withAlpha(double alpha);

	/**
	 * @brief Sets the beta smoothing parameter for the trend.
	 * @param beta The smoothing parameter (0 to 1).
	 * @return A reference to the builder for chaining.
	 */
	HoltLinearTrendBuilder &withBeta(double beta);

	/**
	 * @brief Creates a new HoltLinearTrend model instance.
	 * @return A unique pointer to the configured model.
	 */
	std::unique_ptr<HoltLinearTrend> build();

private:
	double alpha_ = 0.5; // Default alpha
	double beta_ = 0.5;  // Default beta
};

} // namespace anofoxtime::models
