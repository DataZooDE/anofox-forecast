#include "anofox-time/transform/transformers.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <limits>

namespace anofoxtime::transform {

// ============================================================================
// LinearInterpolator
// ============================================================================

void LinearInterpolator::fit(const std::vector<double> &data) {
	// No fitting needed for linear interpolation
}

void LinearInterpolator::transform(std::vector<double> &data) const {
	if (data.empty()) {
		return;
	}
	
	// Find first and last non-NaN values
	size_t first_valid = data.size();
	size_t last_valid = 0;
	
	for (size_t i = 0; i < data.size(); ++i) {
		if (!std::isnan(data[i])) {
			if (first_valid == data.size()) {
				first_valid = i;
			}
			last_valid = i;
		}
	}
	
	// Interpolate interior NaNs
	for (size_t i = first_valid + 1; i < last_valid; ++i) {
		if (std::isnan(data[i])) {
			// Find previous and next valid values
			size_t prev = i - 1;
			size_t next = i + 1;
			
			while (prev > first_valid && std::isnan(data[prev])) {
				--prev;
			}
			while (next < last_valid && std::isnan(data[next])) {
				++next;
			}
			
			if (!std::isnan(data[prev]) && !std::isnan(data[next])) {
				// Linear interpolation
				double weight = static_cast<double>(i - prev) / static_cast<double>(next - prev);
				data[i] = data[prev] + weight * (data[next] - data[prev]);
			}
		}
	}
}

void LinearInterpolator::inverseTransform(std::vector<double> &data) const {
	// Inverse is identity for interpolation
}

// ============================================================================
// Logit
// ============================================================================

void Logit::fit(const std::vector<double> &data) {
	// No fitting needed
}

void Logit::transform(std::vector<double> &data) const {
	for (double &value : data) {
		if (std::isnan(value)) {
			continue;
		}
		// Clamp to (0, 1) to avoid log(0) or log(1)
		value = std::max(std::numeric_limits<double>::epsilon(), 
		                 std::min(1.0 - std::numeric_limits<double>::epsilon(), value));
		value = std::log(value / (1.0 - value));
	}
}

void Logit::inverseTransform(std::vector<double> &data) const {
	for (double &value : data) {
		if (std::isnan(value)) {
			continue;
		}
		value = 1.0 / (1.0 + std::exp(-value));
	}
}

// ============================================================================
// Log
// ============================================================================

void Log::fit(const std::vector<double> &data) {
	// No fitting needed
}

void Log::transform(std::vector<double> &data) const {
	for (double &value : data) {
		if (std::isnan(value) || value <= 0.0) {
			continue;
		}
		value = std::log(value);
	}
}

void Log::inverseTransform(std::vector<double> &data) const {
	for (double &value : data) {
		if (std::isnan(value)) {
			continue;
		}
		value = std::exp(value);
	}
}

// ============================================================================
// MinMaxScaler
// ============================================================================

MinMaxScaler::MinMaxScaler()
	: output_min_(0.0), output_max_(1.0), has_params_(false),
	  input_min_(0.0), input_max_(1.0), scale_factor_(1.0), offset_(0.0) {
}

MinMaxScaler &MinMaxScaler::withScaledRange(double min, double max) {
	output_min_ = min;
	output_max_ = max;
	return *this;
}

MinMaxScaler &MinMaxScaler::withDataRange(double min, double max) {
	input_min_ = min;
	input_max_ = max;
	has_params_ = true;
	computeScale(min, max);
	return *this;
}

void MinMaxScaler::fit(const std::vector<double> &data) {
	if (has_params_) {
		return;  // Already have parameters
	}
	
	// Find min and max, ignoring NaNs
	double min_val = std::numeric_limits<double>::max();
	double max_val = std::numeric_limits<double>::lowest();
	
	for (double value : data) {
		if (!std::isnan(value)) {
			min_val = std::min(min_val, value);
			max_val = std::max(max_val, value);
		}
	}
	
	if (min_val == std::numeric_limits<double>::max()) {
		// All NaNs
		input_min_ = 0.0;
		input_max_ = 1.0;
	} else {
		input_min_ = min_val;
		input_max_ = max_val;
	}
	
	has_params_ = true;
	computeScale(input_min_, input_max_);
}

void MinMaxScaler::transform(std::vector<double> &data) const {
	ensureParams();
	
	for (double &value : data) {
		if (std::isnan(value)) {
			continue;
		}
		value = scale_factor_ * value + offset_;
	}
}

void MinMaxScaler::inverseTransform(std::vector<double> &data) const {
	ensureParams();
	
	for (double &value : data) {
		if (std::isnan(value)) {
			continue;
		}
		value = (value - offset_) / scale_factor_;
	}
}

void MinMaxScaler::ensureParams() const {
	if (!has_params_) {
		throw std::runtime_error("MinMaxScaler must be fitted before transform");
	}
}

void MinMaxScaler::computeScale(double input_min, double input_max) {
	if (std::abs(input_max - input_min) < std::numeric_limits<double>::epsilon()) {
		// Constant data
		scale_factor_ = 1.0;
		offset_ = output_min_;
	} else {
		scale_factor_ = (output_max_ - output_min_) / (input_max - input_min);
		offset_ = output_min_ - scale_factor_ * input_min;
	}
}

// ============================================================================
// StandardScaleParams
// ============================================================================

StandardScaleParams StandardScaleParams::fromData(const std::vector<double> &data) {
	StandardScaleParams params;
	
	// Compute mean
	double sum = 0.0;
	size_t count = 0;
	for (double value : data) {
		if (!std::isnan(value)) {
			sum += value;
			++count;
		}
	}
	
	if (count == 0) {
		return params;  // All NaNs
	}
	
	params.mean = sum / count;
	
	// Compute standard deviation
	double variance = 0.0;
	for (double value : data) {
		if (!std::isnan(value)) {
			double diff = value - params.mean;
			variance += diff * diff;
		}
	}
	
	if (count > 1) {
		params.std_dev = std::sqrt(variance / (count - 1));
	} else {
		params.std_dev = 0.0;
	}
	
	return params;
}

StandardScaleParams StandardScaleParams::fromDataIgnoringNaNs(const std::vector<double> &data) {
	return fromData(data);  // Same implementation
}

// ============================================================================
// StandardScaler
// ============================================================================

StandardScaler::StandardScaler() : ignore_nans_(false), params_(std::nullopt) {
}

StandardScaler &StandardScaler::withParameters(StandardScaleParams params) {
	params_ = params;
	return *this;
}

StandardScaler &StandardScaler::ignoreNaNs(bool ignore) {
	ignore_nans_ = ignore;
	return *this;
}

void StandardScaler::fit(const std::vector<double> &data) {
	if (ignore_nans_) {
		params_ = StandardScaleParams::fromDataIgnoringNaNs(data);
	} else {
		params_ = StandardScaleParams::fromData(data);
	}
}

void StandardScaler::transform(std::vector<double> &data) const {
	ensureParams();
	
	const double mean = params_->mean;
	const double std_dev = params_->std_dev;
	
	if (std::abs(std_dev) < std::numeric_limits<double>::epsilon()) {
		// Constant data - set to zero
		for (double &value : data) {
			if (ignore_nans_ && std::isnan(value)) {
				continue;
			}
			value = 0.0;
		}
	} else {
		for (double &value : data) {
			if (ignore_nans_ && std::isnan(value)) {
				continue;
			}
			if (std::isnan(value)) {
				continue;
			}
			value = (value - mean) / std_dev;
		}
	}
}

void StandardScaler::inverseTransform(std::vector<double> &data) const {
	ensureParams();
	
	const double mean = params_->mean;
	const double std_dev = params_->std_dev;
	
	for (double &value : data) {
		if (ignore_nans_ && std::isnan(value)) {
			continue;
		}
		if (std::isnan(value)) {
			continue;
		}
		value = value * std_dev + mean;
	}
}

void StandardScaler::ensureParams() const {
	if (!params_.has_value()) {
		throw std::runtime_error("StandardScaler must be fitted before transform");
	}
}

// ============================================================================
// BoxCox
// ============================================================================

BoxCox::BoxCox() : lambda_(0.0), has_lambda_(false), ignore_nans_(false) {
}

BoxCox &BoxCox::withLambda(double lambda) {
	lambda_ = lambda;
	has_lambda_ = true;
	return *this;
}

BoxCox &BoxCox::ignoreNaNs(bool ignore) {
	ignore_nans_ = ignore;
	return *this;
}

void BoxCox::fit(const std::vector<double> &data) {
	// For now, we require lambda to be set manually
	// Could implement automatic lambda selection here
	ensureLambda();
}

void BoxCox::transform(std::vector<double> &data) const {
	ensureLambda();
	
	const double eps = std::numeric_limits<double>::epsilon();
	
	if (std::abs(lambda_) < eps) {
		// Log transformation
		for (double &value : data) {
			if (ignore_nans_ && std::isnan(value)) {
				continue;
			}
			if (std::isnan(value) || value <= 0.0) {
				continue;
			}
			value = std::log(value);
		}
	} else {
		// Power transformation: (x^lambda - 1) / lambda
		for (double &value : data) {
			if (ignore_nans_ && std::isnan(value)) {
				continue;
			}
			if (std::isnan(value) || value <= 0.0) {
				continue;
			}
			value = (std::pow(value, lambda_) - 1.0) / lambda_;
		}
	}
}

void BoxCox::inverseTransform(std::vector<double> &data) const {
	ensureLambda();
	
	const double eps = std::numeric_limits<double>::epsilon();
	
	if (std::abs(lambda_) < eps) {
		// Inverse log: exp(x)
		for (double &value : data) {
			if (ignore_nans_ && std::isnan(value)) {
				continue;
			}
			if (std::isnan(value)) {
				continue;
			}
			value = std::exp(value);
		}
	} else {
		// Inverse power: (lambda * x + 1)^(1/lambda)
		for (double &value : data) {
			if (ignore_nans_ && std::isnan(value)) {
				continue;
			}
			if (std::isnan(value)) {
				continue;
			}
			double result = lambda_ * value + 1.0;
			if (result <= 0.0) {
				value = eps;  // Avoid negative/zero
			} else {
				value = std::pow(result, 1.0 / lambda_);
			}
		}
	}
}

std::vector<double> BoxCox::prepareData(const std::vector<double> &data) const {
	if (!ignore_nans_) {
		return data;
	}
	
	std::vector<double> result;
	result.reserve(data.size());
	for (double value : data) {
		if (!std::isnan(value)) {
			result.push_back(value);
		}
	}
	return result;
}

void BoxCox::ensureLambda() const {
	if (!has_lambda_) {
		throw std::runtime_error("BoxCox lambda must be set before transform");
	}
}

// ============================================================================
// YeoJohnson
// ============================================================================

YeoJohnson::YeoJohnson() : lambda_(0.0), has_lambda_(false), ignore_nans_(false) {
}

YeoJohnson &YeoJohnson::withLambda(double lambda) {
	lambda_ = lambda;
	has_lambda_ = true;
	return *this;
}

YeoJohnson &YeoJohnson::ignoreNaNs(bool ignore) {
	ignore_nans_ = ignore;
	return *this;
}

void YeoJohnson::fit(const std::vector<double> &data) {
	// For now, we require lambda to be set manually
	ensureLambda();
}

void YeoJohnson::transform(std::vector<double> &data) const {
	ensureLambda();
	
	const double eps = std::numeric_limits<double>::epsilon();
	
	for (double &value : data) {
		if (ignore_nans_ && std::isnan(value)) {
			continue;
		}
		if (std::isnan(value)) {
			continue;
		}
		
		if (value >= 0.0) {
			// For non-negative values: similar to Box-Cox
			if (std::abs(lambda_) < eps) {
				value = std::log(value + 1.0);
			} else {
				value = (std::pow(value + 1.0, lambda_) - 1.0) / lambda_;
			}
		} else {
			// For negative values: different formula
			if (std::abs(lambda_ - 2.0) < eps) {
				value = -std::log(-value + 1.0);
			} else {
				value = -(std::pow(-value + 1.0, 2.0 - lambda_) - 1.0) / (2.0 - lambda_);
			}
		}
	}
}

void YeoJohnson::inverseTransform(std::vector<double> &data) const {
	ensureLambda();
	
	const double eps = std::numeric_limits<double>::epsilon();
	
	for (double &value : data) {
		if (ignore_nans_ && std::isnan(value)) {
			continue;
		}
		if (std::isnan(value)) {
			continue;
		}
		
		if (value >= 0.0) {
			// Inverse for non-negative
			if (std::abs(lambda_) < eps) {
				value = std::exp(value) - 1.0;
			} else {
				double result = lambda_ * value + 1.0;
				if (result <= 0.0) {
					value = -1.0 + eps;
				} else {
					value = std::pow(result, 1.0 / lambda_) - 1.0;
				}
			}
		} else {
			// Inverse for negative
			if (std::abs(lambda_ - 2.0) < eps) {
				value = 1.0 - std::exp(-value);
			} else {
				double result = (2.0 - lambda_) * (-value) + 1.0;
				if (result <= 0.0) {
					value = 1.0 - eps;
				} else {
					value = 1.0 - std::pow(result, 1.0 / (2.0 - lambda_));
				}
			}
		}
	}
}

std::vector<double> YeoJohnson::prepareData(const std::vector<double> &data) const {
	if (!ignore_nans_) {
		return data;
	}
	
	std::vector<double> result;
	result.reserve(data.size());
	for (double value : data) {
		if (!std::isnan(value)) {
			result.push_back(value);
		}
	}
	return result;
}

void YeoJohnson::ensureLambda() const {
	if (!has_lambda_) {
		throw std::runtime_error("YeoJohnson lambda must be set before transform");
	}
}

} // namespace anofoxtime::transform

