#include "anofox-time/models/naive.hpp"
#include "anofox-time/models/random_walk_drift.hpp"
#include "anofox-time/models/seasonal_naive.hpp"
#include "anofox-time/models/seasonal_window_average.hpp"
#include "anofox-time/models/sma.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/ses_optimized.hpp"
#include "anofox-time/models/holt.hpp"
#include "anofox-time/models/seasonal_es.hpp"
#include "anofox-time/models/seasonal_es_optimized.hpp"
#include "anofox-time/models/holt_winters.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/models/auto_ets.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/optimized_theta.hpp"
#include "anofox-time/models/dynamic_theta.hpp"
#include "anofox-time/models/dynamic_optimized_theta.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/models/auto_arima.hpp"
#include "anofox-time/models/mfles.hpp"
#include "anofox-time/models/auto_mfles.hpp"
#include "anofox-time/models/mstl_forecaster.hpp"
#include "anofox-time/models/auto_mstl.hpp"
#include "anofox-time/models/tbats.hpp"
#include "anofox-time/models/auto_tbats.hpp"
#include "anofox-time/models/ensemble.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/metrics.hpp"
#include "anofox-time/models/croston_classic.hpp"
#include "anofox-time/models/croston_optimized.hpp"
#include "anofox-time/models/croston_sba.hpp"
#include "anofox-time/models/tsb.hpp"
#include "anofox-time/models/adida.hpp"
#include "anofox-time/models/imapa.hpp"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <functional>
#include <map>
#include <cmath>

using namespace anofoxtime;
using namespace std::chrono;

namespace {

// Helper function to compute log-likelihood from residuals (Gaussian assumption)
double computeLogLikelihood(const std::vector<double>& residuals) {
	if (residuals.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	const int n = residuals.size();
	
	// Calculate variance
	double sum_sq = 0.0;
	for (const auto& r : residuals) {
		sum_sq += r * r;
	}
	const double sigma2 = sum_sq / n;
	
	if (sigma2 <= 0.0 || !std::isfinite(sigma2)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Log-likelihood for Gaussian errors
	// ln(L) = -n/2 * ln(2π) - n/2 * ln(σ²) - n/2
	const double log_likelihood = -0.5 * n * std::log(2.0 * M_PI) 
	                             - 0.5 * n * std::log(sigma2) 
	                             - 0.5 * n;
	
	return log_likelihood;
}

// Compute AIC from log-likelihood and parameter count
double computeAIC(double log_likelihood, int k) {
	if (!std::isfinite(log_likelihood)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return 2.0 * k - 2.0 * log_likelihood;
}

// Compute BIC from log-likelihood, parameter count, and sample size
double computeBIC(double log_likelihood, int k, int n) {
	if (!std::isfinite(log_likelihood)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return k * std::log(static_cast<double>(n)) - 2.0 * log_likelihood;
}

// Compute AICc (corrected AIC for small samples)
double computeAICc(double aic, int k, int n) {
	if (!std::isfinite(aic)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	if (n - k - 1 <= 0) {
		return std::numeric_limits<double>::infinity();
	}
	
	return aic + (2.0 * k * (k + 1.0)) / (n - k - 1.0);
}

// Estimate parameter count for different model types
int estimateParameterCount(models::IForecaster* model, int seasonal_period = 1) {
	int k = 0;
	
	// Try to identify model type and estimate parameters
	std::string name = model->getName();
	
	// Naive: 0 parameters (just uses last value)
	if (name == "Naive") return 1;
	
	// RandomWalkWithDrift: 1 parameter (drift)
	if (name == "RandomWalkWithDrift") return 2;
	
	// SeasonalNaive: 0 parameters (just repeats seasonal pattern)
	if (name == "SeasonalNaive") return 1;
	
	// SeasonalWindowAverage: 0 parameters (deterministic)
	if (name == "SeasonalWindowAverage") return 2;
	
	// SMA: 0 parameters (deterministic)
	if (name == "SimpleMovingAverage") return 1;
	
	// SES: 2 parameters (alpha, level)
	if (name == "SimpleExponentialSmoothing") return 2;
	
	// SESOptimized: 2 parameters (optimized alpha, level)
	if (name == "SESOptimized") return 2;
	
	// Holt: 3 parameters (alpha, beta, level)
	if (name == "HoltLinearTrend") return 3;
	
	// SeasonalES: 3 + seasonal_period (alpha, gamma, level, seasonal states)
	if (name == "SeasonalExponentialSmoothing") return 3 + seasonal_period;
	
	// SeasonalESOptimized: 3 + seasonal_period
	if (name == "SeasonalESOptimized") return 3 + seasonal_period;
	
	// HoltWinters: 4 + seasonal_period (alpha, beta, gamma, level, seasonal states)
	if (name == "HoltWinters") return 4 + seasonal_period;
	
	// Theta: 2 parameters (alpha, theta_param)
	if (name == "Theta") return 2;
	
	// OptimizedTheta: 2 parameters (optimized alpha, theta)
	if (name == "OptimizedTheta") return 2;
	
	// DynamicTheta: 3 parameters (alpha, beta for trend)
	if (name == "DynamicTheta") return 3;
	
	// DynamicOptimizedTheta: 3 parameters
	if (name == "DynamicOptimizedTheta") return 3;
	
	// ETS: depends on specification, estimate conservatively
	if (name == "ETS") return 4 + seasonal_period;
	
	// AutoETS: depends on selected model, estimate conservatively
	if (name == "AutoETS") return 4 + seasonal_period;
	
	// MFLES: iterations * components
	if (name == "MFLES") return 6;  // trend + seasonal components
	
	// AutoMFLES: similar to MFLES
	if (name == "AutoMFLES") return 6;
	
	// MSTLForecaster: trend + seasonal parameters
	if (name == "MSTLForecaster") return 4 + seasonal_period;
	
	// AutoMSTL: similar to MSTL
	if (name == "AutoMSTL") return 4 + seasonal_period;
	
	// TBATS: complex, many parameters
	if (name == "TBATS") return 8;
	
	// AutoTBATS: similar to TBATS
	if (name == "AutoTBATS") return 8;
	
	// Ensemble: sum of component parameters (approximate)
	if (name.find("Ensemble") != std::string::npos) {
		// Conservative estimate
		return 5;
	}
	
	// Default conservative estimate
	return 3;
}

// AirPassengers dataset (first 48 months)
std::vector<double> airPassengersData() {
	return {
		112., 118., 132., 129., 121., 135., 148., 148., 136., 119., 104., 118.,
		115., 126., 141., 135., 125., 149., 170., 170., 158., 133., 114., 140.,
		145., 150., 178., 163., 172., 178., 199., 199., 184., 162., 146., 166.,
		171., 180., 193., 181., 183., 218., 230., 242., 209., 191., 172., 194.
	};
}

core::TimeSeries createTimeSeries(const std::vector<double>& data) {
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(data.size());
	auto start = core::TimeSeries::TimePoint{};
	for (std::size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	return core::TimeSeries(std::move(timestamps), data);
}

struct BenchmarkResult {
	std::string method;
	std::string category;
	double mae;
	double rmse;
	double smape;
	double time_ms;
	std::string params;
	std::optional<double> aic;
	std::optional<double> bic;
	std::optional<double> aicc;
};

template<typename ModelFunc>
BenchmarkResult runBenchmark(const std::string& name, const std::string& category,
                             ModelFunc create_and_fit, 
                             const core::TimeSeries& train_ts,
                             const std::vector<double>& test_data,
                             const std::string& params = "") {
	BenchmarkResult result;
	result.method = name;
	result.category = category;
	result.params = params;
	result.mae = 0.0;
	result.rmse = 0.0;
	result.smape = 0.0;
	result.time_ms = 0.0;
	
	try {
		auto start = high_resolution_clock::now();
		
		auto model = create_and_fit();
		model->fit(train_ts);
		auto forecast = model->predict(12);
		
		auto end = high_resolution_clock::now();
		result.time_ms = duration_cast<microseconds>(end - start).count() / 1000.0;
		
		result.mae = utils::Metrics::mae(test_data, forecast.primary());
		result.rmse = utils::Metrics::rmse(test_data, forecast.primary());
		auto smape = utils::Metrics::smape(test_data, forecast.primary());
		result.smape = smape.value_or(0.0);
		
		const int n = train_ts.size();
		
		// Try to extract AIC/BIC/AICc from the model
		// First, try models that already provide AIC/BIC
		if (auto* arima = dynamic_cast<models::ARIMA*>(model.get())) {
			result.aic = arima->aic();
			result.bic = arima->bic();
			if (result.aic.has_value()) {
				int k = estimateParameterCount(model.get());
				result.aicc = computeAICc(*result.aic, k, n);
			}
		}
		else if (auto* tbats = dynamic_cast<models::TBATS*>(model.get())) {
			try {
				result.aic = tbats->aic();
				if (result.aic.has_value()) {
					int k = estimateParameterCount(model.get());
					result.aicc = computeAICc(*result.aic, k, n);
					// Compute BIC from AIC and parameter count
					double log_lik = (2.0 * k - *result.aic) / 2.0;
					result.bic = computeBIC(log_lik, k, n);
				}
			} catch (...) {}
		}
		else if (auto* ets = dynamic_cast<models::ETS*>(model.get())) {
			int k = estimateParameterCount(model.get(), 12);  // Assume seasonal_period=12
			try {
				result.aic = ets->aic(k);
				result.aicc = ets->aicc(k);
				double log_lik = (2.0 * k - *result.aic) / 2.0;
				result.bic = computeBIC(log_lik, k, n);
			} catch (...) {}
		}
		// For all other models, compute from residuals if available
		else {
			// Try to get residuals via dynamic_cast to known types
			const std::vector<double>* residuals_ptr = nullptr;
			
			// Try various model types
			if (auto* naive = dynamic_cast<models::Naive*>(model.get())) {
				residuals_ptr = &naive->residuals();
			}
			else if (auto* rwd = dynamic_cast<models::RandomWalkWithDrift*>(model.get())) {
				residuals_ptr = &rwd->residuals();
			}
			else if (auto* sn = dynamic_cast<models::SeasonalNaive*>(model.get())) {
				residuals_ptr = &sn->residuals();
			}
			else if (auto* swa = dynamic_cast<models::SeasonalWindowAverage*>(model.get())) {
				residuals_ptr = &swa->residuals();
			}
			else if (auto* theta = dynamic_cast<models::Theta*>(model.get())) {
				residuals_ptr = &theta->residuals();
			}
			else if (auto* opt_theta = dynamic_cast<models::OptimizedTheta*>(model.get())) {
				residuals_ptr = &opt_theta->residuals();
			}
			else if (auto* dyn_theta = dynamic_cast<models::DynamicTheta*>(model.get())) {
				residuals_ptr = &dyn_theta->residuals();
			}
			else if (auto* dyn_opt_theta = dynamic_cast<models::DynamicOptimizedTheta*>(model.get())) {
				residuals_ptr = &dyn_opt_theta->residuals();
			}
			else if (auto* mfles = dynamic_cast<models::MFLES*>(model.get())) {
				residuals_ptr = &mfles->residuals();
			}
			else if (auto* hw = dynamic_cast<models::HoltWinters*>(model.get())) {
				residuals_ptr = &hw->residuals();
			}
			else if (auto* ses_opt = dynamic_cast<models::SESOptimized*>(model.get())) {
				residuals_ptr = &ses_opt->residuals();
			}
			else if (auto* ses_opt_seasonal = dynamic_cast<models::SeasonalESOptimized*>(model.get())) {
				residuals_ptr = &ses_opt_seasonal->residuals();
			}
			else if (auto* auto_ets = dynamic_cast<models::AutoETS*>(model.get())) {
				residuals_ptr = &auto_ets->residuals();
			}
			else if (auto* auto_arima = dynamic_cast<models::AutoARIMA*>(model.get())) {
				residuals_ptr = &auto_arima->residuals();
			}
			
			// If we got residuals, compute AIC/BIC/AICc
			if (residuals_ptr && !residuals_ptr->empty()) {
				const double log_lik = computeLogLikelihood(*residuals_ptr);
				
				if (std::isfinite(log_lik)) {
					const int k = estimateParameterCount(model.get(), 12);
					
					result.aic = computeAIC(log_lik, k);
					result.bic = computeBIC(log_lik, k, n);
					result.aicc = computeAICc(*result.aic, k, n);
				}
			}
		}
		
	} catch (const std::exception& e) {
		std::cerr << "  [ERROR] " << name << ": " << e.what() << "\n";
		result.mae = 999.99;
		result.rmse = 999.99;
		result.smape = 999.99;
	}
	
	return result;
}

void printResultsTable(std::vector<BenchmarkResult>& results) {
	// Sort by MAE
	std::sort(results.begin(), results.end(),
	          [](const BenchmarkResult& a, const BenchmarkResult& b) {
		          return a.mae < b.mae;
	          });
	
	std::cout << "  " << std::string(140, '=') << "\n";
	std::cout << "  Rank | Method                       | Category       | MAE     | RMSE    | sMAPE   | AIC      | BIC      | AICc     | Time(ms) | Params\n";
	std::cout << "  " << std::string(140, '-') << "\n";
	
	for (std::size_t i = 0; i < results.size(); ++i) {
		const auto& r = results[i];
		
		// Add medal emojis
		std::string rank_str;
		if (i == 0) rank_str = " 🥇";
		else if (i == 1) rank_str = " 🥈";
		else if (i == 2) rank_str = " 🥉";
		else rank_str = std::to_string(i + 1);
		
		std::cout << "  " << std::setw(4) << std::right << rank_str << " | "
		          << std::setw(28) << std::left << r.method << " | "
		          << std::setw(14) << r.category << " | "
		          << std::fixed << std::setprecision(2) << std::setw(7) << std::right << r.mae << " | "
		          << std::setw(7) << r.rmse << " | "
		          << std::setw(6) << r.smape << "% | ";
		
		// AIC
		if (r.aic.has_value()) {
			std::cout << std::setw(8) << std::fixed << std::setprecision(1) << *r.aic << " | ";
		} else {
			std::cout << std::setw(8) << "N/A" << " | ";
		}
		
		// BIC
		if (r.bic.has_value()) {
			std::cout << std::setw(8) << std::fixed << std::setprecision(1) << *r.bic << " | ";
		} else {
			std::cout << std::setw(8) << "N/A" << " | ";
		}
		
		// AICc
		if (r.aicc.has_value()) {
			std::cout << std::setw(8) << std::fixed << std::setprecision(1) << *r.aicc << " | ";
		} else {
			std::cout << std::setw(8) << "N/A" << " | ";
		}
		
		std::cout << std::setw(8) << std::fixed << std::setprecision(2) << r.time_ms << " | "
		          << std::left << r.params << "\n";
	}
	std::cout << "  " << std::string(140, '=') << "\n";
}

void printCategoryStats(const std::vector<BenchmarkResult>& results) {
	std::map<std::string, std::vector<double>> category_maes;
	
	for (const auto& r : results) {
		if (r.mae < 900.0) {  // Exclude errors
			category_maes[r.category].push_back(r.mae);
		}
	}
	
	std::cout << "\n  Category Performance Summary:\n";
	std::cout << "  " << std::string(60, '-') << "\n";
	std::cout << "  Category       | Best MAE | Avg MAE | Methods\n";
	std::cout << "  " << std::string(60, '-') << "\n";
	
	for (const auto& [category, maes] : category_maes) {
		if (maes.empty()) continue;
		double best = *std::min_element(maes.begin(), maes.end());
		double avg = std::accumulate(maes.begin(), maes.end(), 0.0) / maes.size();
		std::cout << "  " << std::setw(14) << std::left << category << " | "
		          << std::fixed << std::setprecision(2) << std::setw(8) << std::right << best << " | "
		          << std::setw(7) << avg << " | "
		          << maes.size() << "\n";
	}
	std::cout << "  " << std::string(60, '-') << "\n";
}

} // namespace

int main() {
	std::cout << "======================================================================\n";
	std::cout << "           AirPassengers Complete Forecasting Benchmark\n";
	std::cout << "======================================================================\n\n";
	
	// Dataset setup
	const auto air_data = airPassengersData();
	const std::size_t train_size = 36;  // 3 years
	
	std::vector<double> train_data(air_data.begin(), air_data.begin() + train_size);
	std::vector<double> test_data(air_data.begin() + train_size, air_data.end());
	
	auto train_ts = createTimeSeries(train_data);
	
	std::cout << "Dataset:  AirPassengers (classic monthly airline passenger numbers)\n";
	std::cout << "Training: 36 months (Jan 1949 - Dec 1951)\n";
	std::cout << "Testing:  12 months (Jan 1952 - Dec 1952)\n";
	std::cout << "Task:     Forecast 12 months ahead\n\n";
	
	std::cout << "Testing ALL implemented forecasting methods...\n\n";
	
	std::vector<BenchmarkResult> results;
	
	// ===================================================================
	// BASELINE METHODS
	// ===================================================================
	std::cout << "Running Baseline Methods...\n";
	
	results.push_back(runBenchmark(
		"Naive", "Baseline",
		[]() { return std::make_unique<models::Naive>(); },
		train_ts, test_data, "last value"
	));
	
	results.push_back(runBenchmark(
		"RandomWalkWithDrift", "Baseline",
		[]() { return std::make_unique<models::RandomWalkWithDrift>(); },
		train_ts, test_data, "last+drift"
	));
	
	results.push_back(runBenchmark(
		"SeasonalNaive", "Baseline",
		[]() { return std::make_unique<models::SeasonalNaive>(12); },
		train_ts, test_data, "s=12"
	));
	
	results.push_back(runBenchmark(
		"SeasonalWindowAvg(w=2)", "Baseline",
		[]() { return std::make_unique<models::SeasonalWindowAverage>(12, 2); },
		train_ts, test_data, "s=12,w=2"
	));
	
	results.push_back(runBenchmark(
		"SMA(window=0)", "Baseline",
		[]() { return models::SimpleMovingAverageBuilder().withWindow(0).build(); },
		train_ts, test_data, "full history"
	));
	
	// ===================================================================
	// EXPONENTIAL SMOOTHING METHODS
	// ===================================================================
	std::cout << "Running Exponential Smoothing Methods...\n";
	
	results.push_back(runBenchmark(
		"SES(α=0.5)", "Exp.Smooth",
		[]() { return models::SimpleExponentialSmoothingBuilder().withAlpha(0.5).build(); },
		train_ts, test_data, "fixed α"
	));
	
	results.push_back(runBenchmark(
		"SESOptimized", "Exp.Smooth",
		[]() { return std::make_unique<models::SESOptimized>(); },
		train_ts, test_data, "auto α"
	));
	
	results.push_back(runBenchmark(
		"Holt(α=0.8,β=0.2)", "Exp.Smooth",
		[]() { return models::HoltLinearTrendBuilder().withAlpha(0.8).withBeta(0.2).build(); },
		train_ts, test_data, "fixed α,β"
	));
	
	results.push_back(runBenchmark(
		"SeasonalES", "Exp.Smooth",
		[]() { return std::make_unique<models::SeasonalExponentialSmoothing>(12, 0.2, 0.1); },
		train_ts, test_data, "α=0.2,γ=0.1"
	));
	
	results.push_back(runBenchmark(
		"SeasonalESOptimized", "Exp.Smooth",
		[]() { return std::make_unique<models::SeasonalESOptimized>(12); },
		train_ts, test_data, "auto α,γ"
	));
	
	results.push_back(runBenchmark(
		"HoltWinters(Additive)", "Exp.Smooth",
		[]() { return std::make_unique<models::HoltWinters>(12, models::HoltWinters::SeasonType::Additive, 0.2, 0.1, 0.1); },
		train_ts, test_data, "α=0.2,β=0.1,γ=0.1"
	));
	
	// Manual ETS configurations
	results.push_back(runBenchmark(
		"ETS(A,N,A)", "Exp.Smooth",
		[]() {
			models::ETSConfig config;
			config.error = models::ETSErrorType::Additive;
			config.trend = models::ETSTrendType::None;
			config.season = models::ETSSeasonType::Additive;
			config.season_length = 12;
			config.alpha = 0.2;
			config.gamma = 0.1;
			return std::make_unique<models::ETS>(config);
		},
		train_ts, test_data, "α=0.2,γ=0.1"
	));
	
	results.push_back(runBenchmark(
		"ETS(A,A,A)", "Exp.Smooth",
		[]() {
			models::ETSConfig config;
			config.error = models::ETSErrorType::Additive;
			config.trend = models::ETSTrendType::Additive;
			config.season = models::ETSSeasonType::Additive;
			config.season_length = 12;
			config.alpha = 0.2;
			config.beta = 0.1;
			config.gamma = 0.1;
			return std::make_unique<models::ETS>(config);
		},
		train_ts, test_data, "α=0.2,β=0.1,γ=0.1"
	));
	
	results.push_back(runBenchmark(
		"AutoETS", "Exp.Smooth",
		[]() { return std::make_unique<models::AutoETS>(12, "ZZZ"); },
		train_ts, test_data, "auto-select"
	));
	
	// ===================================================================
	// THETA METHODS
	// ===================================================================
	std::cout << "Running Theta Methods...\n";
	
	results.push_back(runBenchmark(
		"Theta(θ=2.0)", "Theta",
		[]() { return std::make_unique<models::Theta>(12, 2.0); },
		train_ts, test_data, "s=12,θ=2.0"
	));
	
	results.push_back(runBenchmark(
		"OptimizedTheta", "Theta",
		[]() { return std::make_unique<models::OptimizedTheta>(12); },
		train_ts, test_data, "auto θ,α"
	));
	
	results.push_back(runBenchmark(
		"DynamicTheta", "Theta",
		[]() { return std::make_unique<models::DynamicTheta>(12); },
		train_ts, test_data, "auto α,β"
	));
	
	results.push_back(runBenchmark(
		"DynamicOptimizedTheta", "Theta",
		[]() { return std::make_unique<models::DynamicOptimizedTheta>(12); },
		train_ts, test_data, "auto α,β"
	));
	
	// ===================================================================
	// ARIMA METHODS
	// ===================================================================
	std::cout << "Running ARIMA Methods...\n";
	
	// Manual SARIMA(0,1,1)(0,1,1)[12] - classic AirPassengers model
	results.push_back(runBenchmark(
		"SARIMA(0,1,1)(0,1,1)[12]", "ARIMA",
		[]() {
			return models::ARIMABuilder()
				.withAR(0).withDifferencing(1).withMA(1)
				.withSeasonalAR(0).withSeasonalDifferencing(1).withSeasonalMA(1)
				.withSeasonalPeriod(12)
				.withIntercept(false)
				.build();
		},
		train_ts, test_data, "classic model"
	));
	
	results.push_back(runBenchmark(
		"AutoARIMA", "ARIMA",
		[]() {
			models::AutoARIMA model(12);
			model.setMaxP(3).setMaxQ(3).setMaxD(2);
			model.setMaxSeasonalP(2).setMaxSeasonalD(1).setMaxSeasonalQ(2);
			model.setStepwise(true);
			return std::make_unique<models::AutoARIMA>(std::move(model));
		},
		train_ts, test_data, "auto-select"
	));
	
	// ===================================================================
	// MFLES (GRADIENT BOOSTING DECOMPOSITION)
	// ===================================================================
	std::cout << "Running MFLES Methods...\n";
	
	results.push_back(runBenchmark(
		"MFLES(default)", "MFLES",
		[]() { return std::make_unique<models::MFLES>(std::vector<int>{12}); },
		train_ts, test_data, "iter=3,lr=0.3/0.5/0.8"
	));
	
	results.push_back(runBenchmark(
		"MFLES(trend-focus)", "MFLES",
		[]() { return std::make_unique<models::MFLES>(std::vector<int>{12}, 3, 0.8, 0.3, 0.3); },
		train_ts, test_data, "lr=0.8/0.3/0.3"
	));
	
	results.push_back(runBenchmark(
		"MFLES(multi-season)", "MFLES",
		[]() { return std::make_unique<models::MFLES>(std::vector<int>{4, 12}); },
		train_ts, test_data, "periods=4+12"
	));
	
	results.push_back(runBenchmark(
		"AutoMFLES", "MFLES",
		[]() { return std::make_unique<models::AutoMFLES>(std::vector<int>{12}); },
		train_ts, test_data, "auto-optimize"
	));
	
	// ===================================================================
	// MSTL (LOESS-BASED DECOMPOSITION)
	// ===================================================================
	std::cout << "Running MSTL Methods...\n";
	
	results.push_back(runBenchmark(
		"MSTL(Linear)", "MSTL",
		[]() { return std::make_unique<models::MSTLForecaster>(std::vector<int>{12}, models::MSTLForecaster::TrendMethod::Linear); },
		train_ts, test_data, "trend=linear"
	));
	
	results.push_back(runBenchmark(
		"MSTL(Holt)", "MSTL",
		[]() { return std::make_unique<models::MSTLForecaster>(std::vector<int>{12}, models::MSTLForecaster::TrendMethod::Holt); },
		train_ts, test_data, "trend=holt"
	));
	
	results.push_back(runBenchmark(
		"MSTL(SES)", "MSTL",
		[]() { return std::make_unique<models::MSTLForecaster>(std::vector<int>{12}, models::MSTLForecaster::TrendMethod::SES); },
		train_ts, test_data, "trend=ses"
	));
	
	// Enhanced MSTL with AutoETS-based forecasting
	results.push_back(runBenchmark(
		"MSTL(AutoETS-T)", "MSTL",
		[]() { return std::make_unique<models::MSTLForecaster>(
			std::vector<int>{12}, 
			models::MSTLForecaster::TrendMethod::AutoETSTrendAdditive,
			models::MSTLForecaster::SeasonalMethod::Cyclic
		); },
		train_ts, test_data, "trend=AutoETS(A),season=cyclic"
	));
	
	results.push_back(runBenchmark(
		"MSTL(AutoETS-S)", "MSTL",
		[]() { return std::make_unique<models::MSTLForecaster>(
			std::vector<int>{12},
			models::MSTLForecaster::TrendMethod::Linear,
			models::MSTLForecaster::SeasonalMethod::AutoETSAdditive
		); },
		train_ts, test_data, "trend=linear,season=AutoETS(A)"
	));
	
	results.push_back(runBenchmark(
		"MSTL(AutoETS-TS)", "MSTL",
		[]() { return std::make_unique<models::MSTLForecaster>(
			std::vector<int>{12},
			models::MSTLForecaster::TrendMethod::AutoETSTrendAdditive,
			models::MSTLForecaster::SeasonalMethod::AutoETSAdditive
		); },
		train_ts, test_data, "trend=AutoETS(A),season=AutoETS(A)"
	));
	
	results.push_back(runBenchmark(
		"AutoMSTL", "MSTL",
		[]() { return std::make_unique<models::AutoMSTL>(std::vector<int>{12}); },
		train_ts, test_data, "auto-optimized"
	));
	
	// ===================================================================
	// TBATS (STATE-SPACE WITH FOURIER)
	// ===================================================================
	std::cout << "Running TBATS Methods...\n";
	
	results.push_back(runBenchmark(
		"TBATS(basic)", "TBATS",
		[]() {
			models::TBATS::Config config;
			config.seasonal_periods = {12};
			return std::make_unique<models::TBATS>(config);
		},
		train_ts, test_data, "default"
	));
	
	results.push_back(runBenchmark(
		"TBATS(Box-Cox)", "TBATS",
		[]() {
			models::TBATS::Config config;
			config.seasonal_periods = {12};
			config.use_box_cox = true;
			config.box_cox_lambda = 0.0;
			return std::make_unique<models::TBATS>(config);
		},
		train_ts, test_data, "λ=0"
	));
	
	results.push_back(runBenchmark(
		"AutoTBATS", "TBATS",
		[]() { return std::make_unique<models::AutoTBATS>(std::vector<int>{12}); },
		train_ts, test_data, "auto-select"
	));
	
	// ===================================================================
	// ENSEMBLE METHODS
	// ===================================================================
	std::cout << "Running Ensemble Methods...\n";
	
	// Mean Ensemble - Baseline Methods
	results.push_back(runBenchmark(
		"Ensemble<Mean>(Baselines)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_shared<models::Naive>());
			forecasters.push_back(std::make_shared<models::SeasonalNaive>(12));
			forecasters.push_back(std::make_shared<models::RandomWalkWithDrift>());
			forecasters.push_back(std::make_shared<models::SeasonalWindowAverage>(12, 2));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "4 baselines"
	));
	
	// Mean Ensemble - Exponential Smoothing
	results.push_back(runBenchmark(
		"Ensemble<Mean>(ExpSmooth)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(models::SimpleExponentialSmoothingBuilder().withAlpha(0.5).build());
			forecasters.push_back(std::make_unique<models::SESOptimized>());
			forecasters.push_back(std::make_unique<models::SeasonalESOptimized>(12));
			forecasters.push_back(std::make_unique<models::HoltWinters>(
				12, models::HoltWinters::SeasonType::Additive, 0.2, 0.1, 0.1));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "4 exp.smooth"
	));
	
	// Mean Ensemble - Theta Methods
	results.push_back(runBenchmark(
		"Ensemble<Mean>(Theta)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_unique<models::Theta>(12, 2.0));
			forecasters.push_back(std::make_unique<models::OptimizedTheta>(12));
			forecasters.push_back(std::make_unique<models::DynamicTheta>(12));
			forecasters.push_back(std::make_unique<models::DynamicOptimizedTheta>(12));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "4 theta"
	));
	
	// Median Ensemble - Diverse Methods
	results.push_back(runBenchmark(
		"Ensemble<Median>(Diverse)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_shared<models::SeasonalNaive>(12));
			forecasters.push_back(std::make_unique<models::SESOptimized>());
			forecasters.push_back(std::make_unique<models::OptimizedTheta>(12));
			forecasters.push_back(std::make_unique<models::AutoETS>(12, "ZZZ"));
			forecasters.push_back(std::make_unique<models::MSTLForecaster>(
				std::vector<int>{12}, models::MSTLForecaster::TrendMethod::Holt));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Median;
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "5 diverse methods"
	));
	
	// Accuracy-Weighted Ensemble - Top Performers
	results.push_back(runBenchmark(
		"Ensemble<AccuracyMAE>(Top)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_unique<models::OptimizedTheta>(12));
			forecasters.push_back(std::make_unique<models::DynamicOptimizedTheta>(12));
			forecasters.push_back(std::make_unique<models::SeasonalESOptimized>(12));
			forecasters.push_back(std::make_unique<models::AutoETS>(12, "ZZZ"));
			forecasters.push_back(std::make_unique<models::MSTLForecaster>(
				std::vector<int>{12}, models::MSTLForecaster::TrendMethod::Holt));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::WeightedAccuracy;
			config.accuracy_metric = models::AccuracyMetric::MAE;
			config.validation_split = 0.25;  // Use last 25% for validation
			config.temperature = 0.5;  // More weight to best performers
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "5 top,weighted,val=25%"
	));
	
	// Mean Ensemble - Best from Each Category
	results.push_back(runBenchmark(
		"Ensemble<Mean>(BestEach)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			// Best from each major category
			forecasters.push_back(std::make_shared<models::SeasonalNaive>(12));  // Baseline
			forecasters.push_back(std::make_unique<models::AutoETS>(12, "ZZZ"));  // Exp.Smooth
			forecasters.push_back(std::make_unique<models::DynamicOptimizedTheta>(12));  // Theta
			forecasters.push_back(std::make_unique<models::MSTLForecaster>(
				std::vector<int>{12}, models::MSTLForecaster::TrendMethod::Holt));  // MSTL
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "4 category winners"
	));
	
	// Lightweight Fast Ensemble
	results.push_back(runBenchmark(
		"Ensemble<Mean>(Fast)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_shared<models::SeasonalNaive>(12));
			forecasters.push_back(models::SimpleExponentialSmoothingBuilder().withAlpha(0.5).build());
			forecasters.push_back(std::make_unique<models::Theta>(12, 2.0));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::Mean;
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "3 fast methods"
	));
	
	// Accuracy-Weighted Ensemble - Aggressive Temperature
	results.push_back(runBenchmark(
		"Ensemble<AccuracyMAE>(Agg)", "Ensemble",
		[]() {
			std::vector<std::shared_ptr<models::IForecaster>> forecasters;
			forecasters.push_back(std::make_unique<models::OptimizedTheta>(12));
			forecasters.push_back(std::make_unique<models::DynamicOptimizedTheta>(12));
			forecasters.push_back(std::make_unique<models::SeasonalESOptimized>(12));
			forecasters.push_back(std::make_unique<models::AutoETS>(12, "ZZZ"));
			
			models::EnsembleConfig config;
			config.method = models::EnsembleCombinationMethod::WeightedAccuracy;
			config.accuracy_metric = models::AccuracyMetric::MAE;
			config.validation_split = 0.3;
			config.temperature = 0.1;  // Very aggressive - mostly best performer
			return std::make_unique<models::Ensemble>(forecasters, config);
		},
		train_ts, test_data, "4 top,temp=0.1,val=30%"
	));
	
	std::cout << "\nAll methods completed!\n\n";
	
	// ===================================================================
	// RESULTS
	// ===================================================================
	std::cout << "======================================================================\n";
	std::cout << "                         COMPLETE RESULTS                            \n";
	std::cout << "======================================================================\n\n";
	
	printResultsTable(results);
	
	// Category stats
	printCategoryStats(results);
	
	// Information Criteria Rankings
	std::cout << "\n  Model Selection by Information Criteria:\n";
	std::cout << "  " << std::string(100, '-') << "\n";
	
	// Best by AIC
	std::vector<BenchmarkResult*> aic_models;
	for (auto& r : results) {
		if (r.aic.has_value() && std::isfinite(*r.aic)) {
			aic_models.push_back(&r);
		}
	}
	std::sort(aic_models.begin(), aic_models.end(),
	         [](const BenchmarkResult* a, const BenchmarkResult* b) {
		         return *a->aic < *b->aic;
	         });
	
	std::cout << "  Best by AIC (lower is better):\n";
	for (std::size_t i = 0; i < std::min<std::size_t>(5, aic_models.size()); ++i) {
		const auto& r = *aic_models[i];
		std::cout << "    " << (i+1) << ". " << std::setw(30) << std::left << r.method 
		          << " AIC=" << std::fixed << std::setprecision(1) << *r.aic
		          << ", MAE=" << std::setprecision(2) << r.mae << "\n";
	}
	
	// Best by BIC
	std::vector<BenchmarkResult*> bic_models;
	for (auto& r : results) {
		if (r.bic.has_value() && std::isfinite(*r.bic)) {
			bic_models.push_back(&r);
		}
	}
	std::sort(bic_models.begin(), bic_models.end(),
	         [](const BenchmarkResult* a, const BenchmarkResult* b) {
		         return *a->bic < *b->bic;
	         });
	
	std::cout << "\n  Best by BIC (lower is better, penalizes complexity more):\n";
	for (std::size_t i = 0; i < std::min<std::size_t>(5, bic_models.size()); ++i) {
		const auto& r = *bic_models[i];
		std::cout << "    " << (i+1) << ". " << std::setw(30) << std::left << r.method 
		          << " BIC=" << std::fixed << std::setprecision(1) << *r.bic
		          << ", MAE=" << std::setprecision(2) << r.mae << "\n";
	}
	
	// Best by AICc
	std::vector<BenchmarkResult*> aicc_models;
	for (auto& r : results) {
		if (r.aicc.has_value() && std::isfinite(*r.aicc)) {
			aicc_models.push_back(&r);
		}
	}
	std::sort(aicc_models.begin(), aicc_models.end(),
	         [](const BenchmarkResult* a, const BenchmarkResult* b) {
		         return *a->aicc < *b->aicc;
	         });
	
	std::cout << "\n  Best by AICc (small sample corrected AIC):\n";
	for (std::size_t i = 0; i < std::min<std::size_t>(5, aicc_models.size()); ++i) {
		const auto& r = *aicc_models[i];
		std::cout << "    " << (i+1) << ". " << std::setw(30) << std::left << r.method 
		          << " AICc=" << std::fixed << std::setprecision(1) << *r.aicc
		          << ", MAE=" << std::setprecision(2) << r.mae << "\n";
	}
	
	std::cout << "\n  Note: Information criteria select based on in-sample fit and model complexity.\n";
	std::cout << "        Lower IC values are better. These may differ from out-of-sample MAE rankings.\n";
	std::cout << "  " << std::string(100, '-') << "\n";
	
	// ===================================================================
	// KEY INSIGHTS
	// ===================================================================
	std::cout << "\n======================================================================\n";
	std::cout << "                         KEY INSIGHTS                                \n";
	std::cout << "======================================================================\n\n";
	
	// Find best overall
	const auto& winner = results[0];
	std::cout << "🏆 WINNER: " << winner.method << " (" << winner.category << ")\n";
	std::cout << "   MAE: " << std::fixed << std::setprecision(2) << winner.mae 
	          << " | Time: " << winner.time_ms << "ms\n\n";
	
	// Best by category
	std::map<std::string, const BenchmarkResult*> best_by_category;
	for (const auto& r : results) {
		if (!best_by_category.count(r.category) || r.mae < best_by_category[r.category]->mae) {
			best_by_category[r.category] = &r;
		}
	}
	
	std::cout << "Best Methods by Category:\n";
	for (const auto& [category, result] : best_by_category) {
		std::cout << "  " << std::setw(15) << std::left << category << ": "
		          << std::setw(30) << result->method 
		          << " (MAE: " << std::fixed << std::setprecision(2) << result->mae << ")\n";
	}
	
	// Speed analysis
	std::cout << "\nSpeed Analysis:\n";
	auto fastest = *std::min_element(results.begin(), results.end(),
	                                 [](const BenchmarkResult& a, const BenchmarkResult& b) {
		                                 return a.time_ms < b.time_ms;
	                                 });
	auto slowest = *std::max_element(results.begin(), results.end(),
	                                 [](const BenchmarkResult& a, const BenchmarkResult& b) {
		                                 return a.time_ms < b.time_ms;
	                                 });
	
	std::cout << "  Fastest: " << fastest.method << " (" << fastest.time_ms << "ms)\n";
	std::cout << "  Slowest: " << slowest.method << " (" << slowest.time_ms << "ms)\n";
	std::cout << "  Speed range: " << std::fixed << std::setprecision(1) 
	          << (slowest.time_ms / fastest.time_ms) << "x difference\n";
	
	// Accuracy vs Speed trade-off
	std::cout << "\nAccuracy vs Speed Trade-off:\n";
	std::cout << "  Best accuracy under 10ms:  ";
	for (const auto& r : results) {
		if (r.time_ms < 10.0 && r.mae < 900.0) {
			std::cout << r.method << " (" << r.mae << " MAE)\n";
			break;
		}
	}
	std::cout << "  Best accuracy under 50ms:  ";
	for (const auto& r : results) {
		if (r.time_ms < 50.0 && r.mae < 900.0) {
			std::cout << r.method << " (" << r.mae << " MAE)\n";
			break;
		}
	}
	std::cout << "  Best accuracy under 200ms: ";
	for (const auto& r : results) {
		if (r.time_ms < 200.0 && r.mae < 900.0) {
			std::cout << r.method << " (" << r.mae << " MAE)\n";
			break;
		}
	}
	
	// Improvement over baseline
	double best_baseline_mae = 999.0;
	for (const auto& r : results) {
		if (r.category == "Baseline" && r.mae < best_baseline_mae) {
			best_baseline_mae = r.mae;
		}
	}
	
	std::cout << "\nImprovement Over Best Baseline (" << best_baseline_mae << " MAE):\n";
	int count_better = 0;
	for (const auto& r : results) {
		if (r.category != "Baseline" && r.mae < best_baseline_mae) {
			double improvement = ((best_baseline_mae - r.mae) / best_baseline_mae) * 100.0;
			if (improvement > 30.0) {  // Only show significant improvements
				std::cout << "  " << std::setw(30) << std::left << r.method 
				          << ": +" << std::fixed << std::setprecision(1) << improvement << "%\n";
				count_better++;
			}
		}
	}
	std::cout << "  Total methods beating baseline by >30%: " << count_better << "\n";
	
	// ===================================================================
	// RECOMMENDATIONS
	// ===================================================================
	std::cout << "\n======================================================================\n";
	std::cout << "                         RECOMMENDATIONS                             \n";
	std::cout << "======================================================================\n\n";
	
	std::cout << "For AirPassengers-like data (seasonal with trend):\n\n";
	
	std::cout << "🏆 Best Accuracy:\n";
	std::cout << "   " << results[0].method << " (MAE: " << results[0].mae << ")\n";
	std::cout << "   Use when: Maximum accuracy is required\n\n";
	
	std::cout << "⚡ Best Speed/Accuracy:\n";
	std::cout << "   Look for methods with MAE < 30 and time < 20ms\n";
	for (std::size_t i = 0; i < std::min<std::size_t>(10, results.size()); ++i) {
		if (results[i].mae < 30.0 && results[i].time_ms < 20.0) {
			std::cout << "   " << results[i].method << " (MAE: " << results[i].mae 
			          << ", " << results[i].time_ms << "ms)\n";
		}
	}
	
	std::cout << "\n📊 For Production Systems:\n";
	std::cout << "   Consider: AutoETS or AutoARIMA for automatic model selection\n";
	std::cout << "   Benefit: Consistent methodology across multiple series\n\n";
	
	std::cout << "🎯 For Benchmarking:\n";
	std::cout << "   Always compare against: SeasonalNaive (simplest seasonal baseline)\n";
	std::cout << "   Your model should beat it by at least 30% to justify complexity\n\n";
	
	// Ensemble-specific insights
	std::cout << "🎭 Ensemble Insights:\n";
	std::vector<const BenchmarkResult*> ensemble_results;
	for (const auto& r : results) {
		if (r.category == "Ensemble") {
			ensemble_results.push_back(&r);
		}
	}
	
	if (!ensemble_results.empty()) {
		auto best_ensemble = *std::min_element(ensemble_results.begin(), ensemble_results.end(),
			[](const BenchmarkResult* a, const BenchmarkResult* b) { return a->mae < b->mae; });
		
		std::cout << "   Best Ensemble: " << best_ensemble->method 
		          << " (MAE: " << best_ensemble->mae << ")\n";
		
		// Compare best ensemble to best non-ensemble
		double best_non_ensemble_mae = 999.0;
		std::string best_non_ensemble_name;
		for (const auto& r : results) {
			if (r.category != "Ensemble" && r.mae < best_non_ensemble_mae) {
				best_non_ensemble_mae = r.mae;
				best_non_ensemble_name = r.method;
			}
		}
		
		if (best_ensemble->mae < best_non_ensemble_mae) {
			double improvement = ((best_non_ensemble_mae - best_ensemble->mae) / best_non_ensemble_mae) * 100.0;
			std::cout << "   Ensemble beats best single model (" << best_non_ensemble_name << ") by "
			          << std::fixed << std::setprecision(1) << improvement << "%\n";
		} else {
			double diff = ((best_ensemble->mae - best_non_ensemble_mae) / best_non_ensemble_mae) * 100.0;
			std::cout << "   Best single model (" << best_non_ensemble_name << ") beats best ensemble by "
			          << std::fixed << std::setprecision(1) << diff << "%\n";
		}
		
		// Ensemble robustness
		double avg_ensemble_mae = 0.0;
		for (const auto* e : ensemble_results) {
			if (e->mae < 900.0) avg_ensemble_mae += e->mae;
		}
		avg_ensemble_mae /= ensemble_results.size();
		
		std::cout << "   Average Ensemble MAE: " << std::fixed << std::setprecision(2) << avg_ensemble_mae << "\n";
		std::cout << "   Ensembles tested: " << ensemble_results.size() << "\n";
		std::cout << "   Benefit: More robust, less sensitive to model selection\n";
	}
	
	return 0;
}
