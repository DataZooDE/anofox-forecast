#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <vector>

namespace anofoxtime::utils {

struct AccuracyMetrics {
	double mae = std::numeric_limits<double>::quiet_NaN();
	double mse = std::numeric_limits<double>::quiet_NaN();
	double rmse = std::numeric_limits<double>::quiet_NaN();
	std::optional<double> mape;
	std::optional<double> smape;
	std::optional<double> mase;
	std::optional<double> r_squared;
	std::size_t n = 0;
	std::vector<AccuracyMetrics> per_dimension;

	bool isMultivariate() const {
		return !per_dimension.empty();
	}
};

class Metrics final {
public:
	static double mae(const std::vector<double> &actual, const std::vector<double> &predicted);
	static double mse(const std::vector<double> &actual, const std::vector<double> &predicted);
	static double rmse(const std::vector<double> &actual, const std::vector<double> &predicted);
	static std::optional<double> mape(const std::vector<double> &actual, const std::vector<double> &predicted);
	static std::optional<double> smape(const std::vector<double> &actual, const std::vector<double> &predicted);
	static std::optional<double> mase(const std::vector<double> &actual, const std::vector<double> &predicted,
	                                  const std::vector<double> &baseline);
	static std::optional<double> r2(const std::vector<double> &actual, const std::vector<double> &predicted);
	static double bias(const std::vector<double> &actual, const std::vector<double> &predicted);
	
	// Relative and quantile-based metrics
	static double rmae(const std::vector<double> &actual, const std::vector<double> &predicted1,
	                   const std::vector<double> &predicted2);
	static double quantile_loss(const std::vector<double> &actual, const std::vector<double> &predicted, double q = 0.5);
	static double mqloss(const std::vector<double> &actual, const std::vector<std::vector<double>> &predicted_quantiles,
	                     const std::vector<double> &quantiles);
	
	// Prediction interval coverage
	static double coverage(const std::vector<double> &actual, const std::vector<double> &lower,
	                       const std::vector<double> &upper);
};

} // namespace anofoxtime::utils
