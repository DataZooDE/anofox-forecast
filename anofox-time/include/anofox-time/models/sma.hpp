#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/logging.hpp"
#include <vector>
#include <stdexcept>

namespace anofoxtime::models {

class SimpleMovingAverageBuilder; // Forward declaration

/**
 * @class SimpleMovingAverage
 * @brief A forecasting model that predicts future values based on the average of a fixed window of past values.
 */
class SimpleMovingAverage final : public IForecaster {
public:
	friend class SimpleMovingAverageBuilder;

	void fit(const core::TimeSeries &ts) override;
	core::Forecast predict(int horizon) override;
	std::string getName() const override {
		return "SimpleMovingAverage";
	}

private:
	/**
	 * @brief Private constructor for SimpleMovingAverage model.
	 * @param window The number of past observations to include in the average.
	 */
	explicit SimpleMovingAverage(int window);

	int window_;
	std::vector<double> history_;
	bool is_fitted_ = false;
};

/**
 * @class SimpleMovingAverageBuilder
 * @brief A builder for fluently configuring and creating SimpleMovingAverage models.
 */
class SimpleMovingAverageBuilder {
public:
	/**
	 * @brief Sets the window size for the moving average.
	 * @param window The number of past observations.
	 * @return A reference to the builder for chaining.
	 */
	SimpleMovingAverageBuilder &withWindow(int window);

	/**
	 * @brief Creates a new SimpleMovingAverage model instance.
	 * @return A unique pointer to the configured model.
	 */
	std::unique_ptr<SimpleMovingAverage> build();

private:
	int window_ = 5; // Default window size
};

} // namespace anofoxtime::models
