#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/logging.hpp"
#include <stdexcept>

namespace anofoxtime::models {

class SimpleExponentialSmoothingBuilder; // Forward declaration

/**
 * @class SimpleExponentialSmoothing
 * @brief A forecasting model that uses a weighted average of past observations,
 *        with the weights decaying exponentially over time.
 */
class SimpleExponentialSmoothing final : public IForecaster {
public:
	friend class SimpleExponentialSmoothingBuilder;

	void fit(const core::TimeSeries &ts) override;
	core::Forecast predict(int horizon) override;
	std::string getName() const override {
		return "SimpleExponentialSmoothing";
	}

private:
	/**
	 * @brief Private constructor for SimpleExponentialSmoothing model.
	 * @param alpha The smoothing parameter for the level, between 0 and 1.
	 */
	explicit SimpleExponentialSmoothing(double alpha);

	double alpha_;
	double last_level_ = 0.0;
	bool is_fitted_ = false;
};

/**
 * @class SimpleExponentialSmoothingBuilder
 * @brief A builder for fluently configuring and creating SimpleExponentialSmoothing models.
 */
class SimpleExponentialSmoothingBuilder {
public:
	/**
	 * @brief Sets the alpha smoothing parameter.
	 * @param alpha The smoothing parameter for the level (0 to 1).
	 * @return A reference to the builder for chaining.
	 */
	SimpleExponentialSmoothingBuilder &withAlpha(double alpha);

	/**
	 * @brief Creates a new SimpleExponentialSmoothing model instance.
	 * @return A unique pointer to the configured model.
	 */
	std::unique_ptr<SimpleExponentialSmoothing> build();

private:
	double alpha_ = 0.5; // Default alpha
};

} // namespace anofoxtime::models
