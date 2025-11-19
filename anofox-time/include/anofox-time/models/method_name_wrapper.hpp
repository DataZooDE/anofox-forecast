#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <memory>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Generic wrapper that overrides the method name of any IForecaster.
 * 
 * This wrapper allows users to customize the method name returned by getName()
 * while preserving all other functionality of the wrapped model. All forecasting
 * operations are delegated to the underlying model.
 * 
 * Use case: When you want to distinguish between similar models or provide
 * custom naming for tracking/comparison purposes.
 */
class MethodNameWrapper : public IForecaster {
public:
	/**
	 * @brief Construct a MethodNameWrapper
	 * @param wrapped_model The model to wrap (ownership transferred)
	 * @param custom_name The custom name to return from getName()
	 * @throws std::invalid_argument if custom_name is empty or wrapped_model is null
	 */
	MethodNameWrapper(std::unique_ptr<IForecaster> wrapped_model, 
	                  std::string custom_name);
	
	// IForecaster interface
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return custom_name_;
	}
	
	/**
	 * @brief Access the wrapped model
	 * @return Reference to the underlying IForecaster
	 */
	IForecaster& wrappedModel() {
		return *wrapped_model_;
	}
	
	const IForecaster& wrappedModel() const {
		return *wrapped_model_;
	}

private:
	std::unique_ptr<IForecaster> wrapped_model_;
	std::string custom_name_;
};

} // namespace anofoxtime::models



