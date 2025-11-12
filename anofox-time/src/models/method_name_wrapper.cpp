#include "anofox-time/models/method_name_wrapper.hpp"
#include "anofox-time/utils/logging.hpp"
#include <stdexcept>

namespace anofoxtime::models {

MethodNameWrapper::MethodNameWrapper(std::unique_ptr<IForecaster> wrapped_model,
                                     std::string custom_name)
    : wrapped_model_(std::move(wrapped_model)), custom_name_(std::move(custom_name)) {
	
	if (!wrapped_model_) {
		throw std::invalid_argument("MethodNameWrapper: wrapped_model cannot be null");
	}
	
	if (custom_name_.empty()) {
		throw std::invalid_argument("MethodNameWrapper: custom_name cannot be empty");
	}
	
	ANOFOX_INFO("MethodNameWrapper created: wrapping '{}' as '{}'",
	            wrapped_model_->getName(), custom_name_);
}

void MethodNameWrapper::fit(const core::TimeSeries& ts) {
	if (!wrapped_model_) {
		throw std::runtime_error("MethodNameWrapper: wrapped_model is null");
	}
	
	ANOFOX_DEBUG("MethodNameWrapper '{}': delegating fit to underlying model '{}'",
	             custom_name_, wrapped_model_->getName());
	
	wrapped_model_->fit(ts);
}

core::Forecast MethodNameWrapper::predict(int horizon) {
	if (!wrapped_model_) {
		throw std::runtime_error("MethodNameWrapper: wrapped_model is null");
	}
	
	ANOFOX_DEBUG("MethodNameWrapper '{}': delegating predict to underlying model '{}'",
	             custom_name_, wrapped_model_->getName());
	
	return wrapped_model_->predict(horizon);
}

} // namespace anofoxtime::models


