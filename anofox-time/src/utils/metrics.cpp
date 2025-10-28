#include "anofox-time/utils/metrics.hpp"
#include <numeric>

namespace anofoxtime::utils {

namespace {

void validate_lengths(const std::vector<double> &actual, const std::vector<double> &predicted) {
	if (actual.size() != predicted.size() || actual.empty()) {
		throw std::invalid_argument("Actual and predicted vectors must be non-empty and equal length.");
	}
}

} // namespace

double Metrics::mae(const std::vector<double> &actual, const std::vector<double> &predicted) {
	validate_lengths(actual, predicted);
	double sum = 0.0;
	for (size_t i = 0; i < actual.size(); ++i) {
		sum += std::abs(actual[i] - predicted[i]);
	}
	return sum / static_cast<double>(actual.size());
}

double Metrics::mse(const std::vector<double> &actual, const std::vector<double> &predicted) {
	validate_lengths(actual, predicted);
	double sum = 0.0;
	for (size_t i = 0; i < actual.size(); ++i) {
		const double diff = actual[i] - predicted[i];
		sum += diff * diff;
	}
	return sum / static_cast<double>(actual.size());
}

double Metrics::rmse(const std::vector<double> &actual, const std::vector<double> &predicted) {
	return std::sqrt(mse(actual, predicted));
}

std::optional<double> Metrics::mape(const std::vector<double> &actual, const std::vector<double> &predicted) {
	validate_lengths(actual, predicted);
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = 0; i < actual.size(); ++i) {
		const double denom = std::abs(actual[i]);
		if (denom > std::numeric_limits<double>::epsilon()) {
			sum += std::abs(actual[i] - predicted[i]) / denom;
			++count;
		}
	}
	if (count == 0)
		return std::nullopt;
	return (sum / static_cast<double>(count)) * 100.0;
}

std::optional<double> Metrics::smape(const std::vector<double> &actual, const std::vector<double> &predicted) {
	validate_lengths(actual, predicted);
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = 0; i < actual.size(); ++i) {
		const double denom = (std::abs(actual[i]) + std::abs(predicted[i])) / 2.0;
		if (denom > std::numeric_limits<double>::epsilon()) {
			sum += std::abs(actual[i] - predicted[i]) / denom;
			++count;
		}
	}
	if (count == 0)
		return std::nullopt;
	return (sum / static_cast<double>(count)) * 100.0;
}

std::optional<double> Metrics::mase(const std::vector<double> &actual, const std::vector<double> &predicted,
                                    const std::vector<double> &baseline) {
	validate_lengths(actual, predicted);
	if (baseline.size() != actual.size()) {
		throw std::invalid_argument("Baseline vector must match actual size for MASE.");
	}

	const double mae_forecast = mae(actual, predicted);
	double mae_baseline = 0.0;
	for (size_t i = 0; i < actual.size(); ++i) {
		mae_baseline += std::abs(actual[i] - baseline[i]);
	}
	mae_baseline /= static_cast<double>(actual.size());

	if (std::abs(mae_baseline) < std::numeric_limits<double>::epsilon()) {
		return std::nullopt;
	}

	return mae_forecast / mae_baseline;
}

std::optional<double> Metrics::r2(const std::vector<double> &actual, const std::vector<double> &predicted) {
	validate_lengths(actual, predicted);

	const double mean_actual = std::accumulate(actual.begin(), actual.end(), 0.0) / static_cast<double>(actual.size());

	double ss_res = 0.0;
	double ss_tot = 0.0;
	for (size_t i = 0; i < actual.size(); ++i) {
		const double diff_res = actual[i] - predicted[i];
		ss_res += diff_res * diff_res;

		const double diff_tot = actual[i] - mean_actual;
		ss_tot += diff_tot * diff_tot;
	}

	if (std::abs(ss_tot) < std::numeric_limits<double>::epsilon()) {
		return std::nullopt;
	}

	return 1.0 - (ss_res / ss_tot);
}

double Metrics::bias(const std::vector<double> &actual, const std::vector<double> &predicted) {
	validate_lengths(actual, predicted);
	double sum = 0.0;
	for (size_t i = 0; i < actual.size(); ++i) {
		sum += (predicted[i] - actual[i]);
	}
	return sum / static_cast<double>(actual.size());
}

double Metrics::rmae(const std::vector<double> &actual, const std::vector<double> &predicted1,
                     const std::vector<double> &predicted2) {
	// Relative Mean Absolute Error
	// Compares two forecasting methods by dividing MAE of first by MAE of second
	// A value < 1 means predicted1 is better than predicted2
	validate_lengths(actual, predicted1);
	validate_lengths(actual, predicted2);
	
	const double mae1 = mae(actual, predicted1);
	const double mae2 = mae(actual, predicted2);
	
	if (std::abs(mae2) < std::numeric_limits<double>::epsilon()) {
		throw std::invalid_argument("MAE of baseline model (predicted2) is zero, RMAE undefined.");
	}
	
	return mae1 / mae2;
}

double Metrics::quantile_loss(const std::vector<double> &actual, const std::vector<double> &predicted, double q) {
	// Quantile Loss (Pinball Loss)
	// Measures deviation from a specific quantile forecast
	// q = 0.5 gives median (equal weight to over/under-prediction)
	validate_lengths(actual, predicted);
	
	if (q <= 0.0 || q >= 1.0) {
		throw std::invalid_argument("Quantile q must be in the range (0, 1).");
	}
	
	double sum = 0.0;
	for (size_t i = 0; i < actual.size(); ++i) {
		const double error = actual[i] - predicted[i];
		if (error > 0.0) {
			sum += q * error;  // Under-prediction penalty
		} else {
			sum += (q - 1.0) * error;  // Over-prediction penalty
		}
	}
	
	return sum / static_cast<double>(actual.size());
}

double Metrics::mqloss(const std::vector<double> &actual, const std::vector<std::vector<double>> &predicted_quantiles,
                       const std::vector<double> &quantiles) {
	// Multi-Quantile Loss
	// Averages quantile loss over multiple quantiles
	// Used for evaluating full predictive distributions (CRPS approximation)
	
	if (predicted_quantiles.empty() || quantiles.empty()) {
		throw std::invalid_argument("Predicted quantiles and quantiles vectors must be non-empty.");
	}
	
	if (predicted_quantiles.size() != quantiles.size()) {
		throw std::invalid_argument("Number of predicted quantile series must match number of quantiles.");
	}
	
	// Validate all predicted quantile series have same length as actual
	for (const auto &pq : predicted_quantiles) {
		validate_lengths(actual, pq);
	}
	
	double total_loss = 0.0;
	for (size_t q_idx = 0; q_idx < quantiles.size(); ++q_idx) {
		total_loss += quantile_loss(actual, predicted_quantiles[q_idx], quantiles[q_idx]);
	}
	
	return total_loss / static_cast<double>(quantiles.size());
}

double Metrics::coverage(const std::vector<double> &actual, const std::vector<double> &lower,
                         const std::vector<double> &upper) {
	const auto n = actual.size();
	if (n == 0) {
		throw std::invalid_argument("Metrics::coverage: Arrays must not be empty");
	}
	if (lower.size() != n || upper.size() != n) {
		throw std::invalid_argument("Metrics::coverage: Arrays must have the same length");
	}
	
	std::size_t in_interval = 0;
	for (std::size_t i = 0; i < n; ++i) {
		if (!std::isfinite(actual[i]) || !std::isfinite(lower[i]) || !std::isfinite(upper[i])) {
			throw std::invalid_argument("Metrics::coverage: All values must be finite");
		}
		if (lower[i] > upper[i]) {
			throw std::invalid_argument("Metrics::coverage: Lower bound must be <= upper bound");
		}
		if (actual[i] >= lower[i] && actual[i] <= upper[i]) {
			++in_interval;
		}
	}
	
	return static_cast<double>(in_interval) / static_cast<double>(n);
}

} // namespace anofoxtime::utils
