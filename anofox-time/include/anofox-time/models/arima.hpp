#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/logging.hpp"
#include "anofox-time/utils/metrics.hpp"
#include <Eigen/Dense>
#include <optional>
#include <stdexcept>
#include <vector>

namespace anofoxtime::models {

class ARIMABuilder; // Forward declaration

class ARIMA final : public IForecaster {
public:
	friend class ARIMABuilder;

	void fit(const core::TimeSeries &ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);

	std::string getName() const override {
		return "ARIMA";
	}

	const Eigen::VectorXd &arCoefficients() const {
		return ar_coeffs_;
	}
	const Eigen::VectorXd &maCoefficients() const {
		return ma_coeffs_;
	}
	const Eigen::VectorXd &seasonalARCoefficients() const {
		return seasonal_ar_coeffs_;
	}
	const Eigen::VectorXd &seasonalMACoefficients() const {
		return seasonal_ma_coeffs_;
	}
	const std::vector<double> &residuals() const {
		return residuals_;
	}
	int seasonalPeriod() const {
		return seasonal_period_;
	}
	std::optional<double> aic() const {
		return aic_;
	}
	std::optional<double> bic() const {
		return bic_;
	}

	// Static utility methods (public for testing and flexibility)
	static std::vector<double> difference(const std::vector<double> &data, int d);
	static std::vector<double> integrate(const std::vector<double> &forecast_diff,
	                                     const std::vector<double> &last_values, int d);
	static std::vector<double> seasonalDifference(const std::vector<double> &data, int D, int s);
	static std::vector<double> seasonalIntegrate(const std::vector<double> &forecast_diff,
	                                             const std::vector<double> &last_values, int D, int s);
	static std::vector<double> combinedDifference(const std::vector<double> &data, int d, int D, int s);

private:
	explicit ARIMA(int p, int d, int q, int P, int D, int Q, int s, bool include_intercept);

	static double normalQuantile(double p);
	static double logLikelihood(const std::vector<double> &residuals);

	int p_, d_, q_;
	int P_, D_, Q_;
	int seasonal_period_;
	bool include_intercept_;
	Eigen::VectorXd ar_coeffs_;
	Eigen::VectorXd ma_coeffs_;
	Eigen::VectorXd seasonal_ar_coeffs_;
	Eigen::VectorXd seasonal_ma_coeffs_;
	double intercept_ = 0.0;
	std::vector<double> history_;
	std::vector<double> differenced_history_;
	std::vector<double> last_values_;
	std::vector<double> seasonal_last_values_;
	std::vector<double> residuals_;
	std::vector<double> last_residuals_;
	std::vector<double> seasonal_last_residuals_;
	std::vector<double> fitted_values_;
	double mean_ = 0.0;
	double sigma2_ = 0.0;
	std::optional<double> aic_;
	std::optional<double> bic_;
	bool is_fitted_ = false;
};

class ARIMABuilder {
public:
	ARIMABuilder &withAR(int p);
	ARIMABuilder &withDifferencing(int d);
	ARIMABuilder &withMA(int q);
	ARIMABuilder &withSeasonalAR(int P);
	ARIMABuilder &withSeasonalDifferencing(int D);
	ARIMABuilder &withSeasonalMA(int Q);
	ARIMABuilder &withSeasonalPeriod(int s);
	ARIMABuilder &withIntercept(bool include_intercept);
	std::unique_ptr<ARIMA> build();

private:
	int p_ = 0;
	int d_ = 0;
	int q_ = 0;
	int P_ = 0;
	int D_ = 0;
	int Q_ = 0;
	int s_ = 0;
	bool include_intercept_ = true;
};

} // namespace anofoxtime::models
