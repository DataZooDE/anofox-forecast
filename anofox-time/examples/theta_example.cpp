#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/optimized_theta.hpp"
#include "anofox-time/models/dynamic_theta.hpp"
#include "anofox-time/models/dynamic_optimized_theta.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/metrics.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>

using namespace anofoxtime;

namespace {

// Generate synthetic data
std::vector<double> generateTrendingData(std::size_t n, double slope = 0.5, double intercept = 100.0) {
	std::vector<double> data(n);
	for (std::size_t i = 0; i < n; ++i) {
		data[i] = intercept + slope * static_cast<double>(i) + (rand() % 10 - 5) * 0.1;
	}
	return data;
}

std::vector<double> generateSeasonalData(std::size_t cycles, int period = 12) {
	std::vector<double> data;
	data.reserve(cycles * static_cast<size_t>(period));
	
	for (std::size_t c = 0; c < cycles; ++c) {
		for (int t = 0; t < period; ++t) {
			const double seasonal = 10.0 * std::sin(2.0 * M_PI * static_cast<double>(t) / static_cast<double>(period));
			const double trend = 100.0 + 0.5 * static_cast<double>(c * period + t);
			const double noise = (rand() % 10 - 5) * 0.2;
			data.push_back(trend + seasonal + noise);
		}
	}
	return data;
}

// AirPassengers-like data (first 48 months)
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

void printHeader(const std::string& title) {
	std::cout << "\n=== " << title << " ===\n\n";
}

void printForecast(const std::string& method, const core::Forecast& forecast, int show_n = 5) {
	std::cout << method << " forecast (first " << show_n << " points):\n  ";
	const auto& values = forecast.primary();
	for (int i = 0; i < std::min(show_n, static_cast<int>(values.size())); ++i) {
		std::cout << std::fixed << std::setprecision(2) << values[i];
		if (i < show_n - 1 && i < static_cast<int>(values.size()) - 1) std::cout << ", ";
	}
	std::cout << "\n";
	std::cout.unsetf(std::ios::floatfield);
}

void printMetrics(const std::vector<double>& actual, const std::vector<double>& forecast, const std::string& method) {
	if (actual.size() != forecast.size() || actual.empty()) {
		return;
	}
	
	const double mae = utils::Metrics::mae(actual, forecast);
	const double rmse = utils::Metrics::rmse(actual, forecast);
	const auto smape_opt = utils::Metrics::smape(actual, forecast);
	
	std::cout << method << " Accuracy:\n";
	std::cout << "  MAE:   " << std::fixed << std::setprecision(4) << mae << '\n';
	std::cout << "  RMSE:  " << std::fixed << std::setprecision(4) << rmse << '\n';
	if (smape_opt && std::isfinite(*smape_opt)) {
		std::cout << "  sMAPE: " << std::fixed << std::setprecision(4) << *smape_opt << '\n';
	}
	std::cout.unsetf(std::ios::floatfield);
}

} // namespace

int main() {
	std::cout << "=== Theta Family Forecasting Methods Examples ===\n";
	std::cout << "Demonstrating Theta, OptimizedTheta, DynamicTheta, and DynamicOptimizedTheta\n";
	
	// ===================================================================
	// Scenario 1: Trending Data (Non-Seasonal)
	// ===================================================================
	printHeader("Scenario 1: Trending Data");
	
	const auto trending_data = generateTrendingData(100, 0.5, 100.0);
	auto ts_trend = createTimeSeries(trending_data);
	
	std::cout << "Training data: 100 points\n";
	std::cout << "Forecast horizon: 10 points\n\n";
	
	// Classic Theta (theta=2)
	models::Theta theta_classic(1, 2.0);
	theta_classic.fit(ts_trend);
	auto forecast_classic = theta_classic.predict(10);
	printForecast("  Theta (θ=2.0)", forecast_classic);
	
	// Optimized Theta
	models::OptimizedTheta theta_opt(1);
	theta_opt.fit(ts_trend);
	auto forecast_opt = theta_opt.predict(10);
	std::cout << "  OptimizedTheta (θ=" << std::fixed << std::setprecision(2) 
	          << theta_opt.getOptimalTheta() << ", α=" << theta_opt.getOptimalAlpha() << ")\n";
	printForecast("    ", forecast_opt);
	
	// Dynamic Theta
	models::DynamicTheta theta_dyn(1);
	theta_dyn.fit(ts_trend);
	auto forecast_dyn = theta_dyn.predict(10);
	std::cout << "  DynamicTheta (α=" << std::fixed << std::setprecision(3) 
	          << theta_dyn.getAlpha() << ", θ=" << theta_dyn.getTheta() << ")\n";
	printForecast("    ", forecast_dyn);
	
	// Dynamic Optimized Theta (M4 competition method)
	models::DynamicOptimizedTheta theta_dot(1);
	theta_dot.fit(ts_trend);
	auto forecast_dot = theta_dot.predict(10);
	std::cout << "  DynamicOptimizedTheta (α=" << std::fixed << std::setprecision(3) 
	          << theta_dot.getOptimalAlpha() << ", θ=" << theta_dot.getOptimalTheta() << ")\n";
	printForecast("    ", forecast_dot);
	
	// ===================================================================
	// Scenario 2: Seasonal Data (Monthly)
	// ===================================================================
	printHeader("Scenario 2: Seasonal Data (Monthly)");
	
	const auto seasonal_data = generateSeasonalData(10, 12);
	auto ts_seasonal = createTimeSeries(seasonal_data);
	
	std::cout << "Training data: 120 points (10 years of monthly data)\n";
	std::cout << "Forecast horizon: 12 points (1 year)\n";
	std::cout << "Seasonal period: 12 (monthly)\n\n";
	
	// Classic Theta with seasonality
	models::Theta theta_seas(12, 2.0);
	theta_seas.fit(ts_seasonal);
	auto forecast_seas = theta_seas.predict(12);
	printForecast("  Theta (s=12, θ=2.0)", forecast_seas);
	
	// Optimized Theta with seasonality
	models::OptimizedTheta theta_opt_seas(12);
	theta_opt_seas.fit(ts_seasonal);
	auto forecast_opt_seas = theta_opt_seas.predict(12);
	std::cout << "  OptimizedTheta (s=12, θ=" << std::fixed << std::setprecision(2) 
	          << theta_opt_seas.getOptimalTheta() << ")\n";
	printForecast("    ", forecast_opt_seas);
	
	// Dynamic Optimized Theta with seasonality
	models::DynamicOptimizedTheta theta_dot_seas(12);
	theta_dot_seas.fit(ts_seasonal);
	auto forecast_dot_seas = theta_dot_seas.predict(12);
	std::cout << "  DynamicOptimizedTheta (s=12)\n";
	printForecast("    ", forecast_dot_seas);
	
	// ===================================================================
	// Scenario 3: AirPassengers Benchmark
	// ===================================================================
	printHeader("Scenario 3: AirPassengers Benchmark");
	
	const auto air_data = airPassengersData();
	
	// Split into train and test
	const std::size_t train_size = 36;  // 3 years
	const std::size_t test_size = air_data.size() - train_size;  // 1 year
	
	std::vector<double> train_data(air_data.begin(), air_data.begin() + train_size);
	std::vector<double> test_data(air_data.begin() + train_size, air_data.end());
	
	auto ts_air = createTimeSeries(train_data);
	
	std::cout << "Classic AirPassengers dataset\n";
	std::cout << "Training: " << train_size << " months\n";
	std::cout << "Testing: " << test_size << " months\n";
	std::cout << "Seasonal period: 12 (monthly)\n\n";
	
	// Theta
	models::Theta air_theta(12, 2.0);
	air_theta.fit(ts_air);
	auto forecast_air_theta = air_theta.predict(static_cast<int>(test_size));
	printForecast("  Theta forecast", forecast_air_theta);
	printMetrics(test_data, forecast_air_theta.primary(), "  Theta");
	
	// OptimizedTheta
	models::OptimizedTheta air_opt(12);
	air_opt.fit(ts_air);
	auto forecast_air_opt = air_opt.predict(static_cast<int>(test_size));
	printForecast("\n  OptimizedTheta forecast", forecast_air_opt);
	printMetrics(test_data, forecast_air_opt.primary(), "  OptimizedTheta");
	
	// DynamicOptimizedTheta (best for competitions)
	models::DynamicOptimizedTheta air_dot(12);
	air_dot.fit(ts_air);
	auto forecast_air_dot = air_dot.predict(static_cast<int>(test_size));
	printForecast("\n  DynamicOptimizedTheta forecast", forecast_air_dot);
	printMetrics(test_data, forecast_air_dot.primary(), "  DynamicOptimizedTheta");
	
	// ===================================================================
	// Scenario 4: Confidence Intervals
	// ===================================================================
	printHeader("Scenario 4: Confidence Intervals");
	
	std::cout << "Using DynamicOptimizedTheta with 95% confidence intervals\n";
	std::cout << "Data: 50 trending points\n\n";
	
	const auto ci_data = generateTrendingData(50, 0.3, 80.0);
	auto ts_ci = createTimeSeries(ci_data);
	
	models::DynamicOptimizedTheta ci_model(1);
	ci_model.fit(ts_ci);
	auto forecast_ci = ci_model.predictWithConfidence(10, 0.95);
	
	std::cout << "Forecast with 95% confidence intervals:\n";
	std::cout << "  Step | Forecast |   Lower  |   Upper  | Width\n";
	std::cout << "  -----|----------|----------|----------|------\n";
	
	for (std::size_t i = 0; i < std::min<std::size_t>(10, forecast_ci.primary().size()); ++i) {
		const double width = forecast_ci.upperSeries()[i] - forecast_ci.lowerSeries()[i];
		std::cout << "  " << std::setw(4) << (i+1) << " | "
		          << std::fixed << std::setprecision(2) << std::setw(8) << forecast_ci.primary()[i] << " | "
		          << std::setw(8) << forecast_ci.lowerSeries()[i] << " | "
		          << std::setw(8) << forecast_ci.upperSeries()[i] << " | "
		          << std::setw(5) << width << "\n";
	}
	
	// ===================================================================
	// Summary
	// ===================================================================
	printHeader("Summary");
	
	std::cout << "Theta Method Family:\n";
	std::cout << "  • Theta: Classic method with fixed θ parameter (usually 2.0)\n";
	std::cout << "  • OptimizedTheta: Optimizes θ and α parameters via grid search\n";
	std::cout << "  • DynamicTheta: Uses state space (Holt's method) with optimization\n";
	std::cout << "  • DynamicOptimizedTheta: M4 competition winner component\n\n";
	
	std::cout << "Use Cases:\n";
	std::cout << "  • Theta: Quick forecasts, baseline comparison\n";
	std::cout << "  • OptimizedTheta: Better accuracy for non-seasonal data\n";
	std::cout << "  • DynamicTheta: Trending data with evolving patterns\n";
	std::cout << "  • DynamicOptimizedTheta: Competition-grade accuracy\n\n";
	
	std::cout << "Reference:\n";
	std::cout << "  • Assimakopoulos & Nikolopoulos (2000) - Original Theta method\n";
	std::cout << "  • Fiorucci et al. (2016) - Optimized Theta variants\n";
	std::cout << "  • Petropoulos & Svetunkov (2020) - M4 competition ensemble\n";
	
	return 0;
}

