#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/arima.hpp"
#include "common/time_series_helpers.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

using anofoxtime::models::ARIMABuilder;

namespace {

// Generate seasonal data with known pattern
std::vector<double> generateSeasonalData(std::size_t cycles, int period = 12) {
	std::vector<double> data;
	data.reserve(cycles * static_cast<std::size_t>(period));
	
	for (std::size_t c = 0; c < cycles; ++c) {
		for (int t = 0; t < period; ++t) {
			const double seasonal = 10.0 * std::sin(2.0 * M_PI * static_cast<double>(t) / static_cast<double>(period));
			const double base = 100.0 + 0.1 * static_cast<double>(c * period + t);
			data.push_back(base + seasonal);
		}
	}
	return data;
}

// Classic AirPassengers dataset (first 48 months)
std::vector<double> airPassengersData() {
	return {
		112., 118., 132., 129., 121., 135., 148., 148., 136., 119., 104., 118.,
		115., 126., 141., 135., 125., 149., 170., 170., 158., 133., 114., 140.,
		145., 150., 178., 163., 172., 178., 199., 199., 184., 162., 146., 166.,
		171., 180., 193., 181., 183., 218., 230., 242., 209., 191., 172., 194.
	};
}

// Quarterly seasonal data
std::vector<double> generateQuarterlySeasonal(std::size_t cycles) {
	std::vector<double> data;
	const int period = 4;
	data.reserve(cycles * period);
	
	const std::vector<double> seasonal_pattern = {-5.0, 2.0, 8.0, -3.0};
	for (std::size_t c = 0; c < cycles; ++c) {
		for (int t = 0; t < period; ++t) {
			data.push_back(50.0 + seasonal_pattern[t] + 0.2 * static_cast<double>(c * period + t));
		}
	}
	return data;
}

} // namespace

TEST_CASE("SARIMA builder accepts seasonal parameters", "[models][sarima][builder]") {
	// Should accept valid seasonal parameters
	REQUIRE_NOTHROW(
		ARIMABuilder()
			.withAR(1).withDifferencing(1).withMA(1)
			.withSeasonalAR(1).withSeasonalDifferencing(1).withSeasonalMA(1)
			.withSeasonalPeriod(12)
			.build()
	);
	
	// Should reject seasonal components with period < 2
	REQUIRE_THROWS_AS(
		ARIMABuilder()
			.withSeasonalAR(1)
			.withSeasonalPeriod(1)
			.build(),
		std::invalid_argument
	);
}

TEST_CASE("SARIMA backward compatibility with non-seasonal ARIMA", "[models][sarima][backward_compat]") {
	const std::vector<double> data = {10., 12., 15., 14., 16., 18., 20., 19., 22., 24.,
	                                   26., 25., 28., 30., 32., 31., 34., 36., 38., 37.};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,1,1)(0,0,0)[0] should behave like ARIMA(1,1,1)
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(1).withMA(1)
		.withSeasonalAR(0).withSeasonalDifferencing(0).withSeasonalMA(0)
		.withSeasonalPeriod(0)
		.withIntercept(true)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE_NOTHROW(model->predict(3));
	
	REQUIRE(model->arCoefficients().size() == 1);
	REQUIRE(model->maCoefficients().size() == 1);
	REQUIRE(model->seasonalARCoefficients().size() == 0);
	REQUIRE(model->seasonalMACoefficients().size() == 0);
}

TEST_CASE("SARIMA seasonal differencing works correctly", "[models][sarima][differencing]") {
	const std::vector<double> data = generateSeasonalData(5, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(0,0,0)(0,1,0)[12] - just seasonal differencing
	auto model = ARIMABuilder()
		.withAR(0).withDifferencing(0).withMA(1)  // Need at least MA to be valid
		.withSeasonalDifferencing(1)
		.withSeasonalPeriod(12)
		.withIntercept(false)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// Forecasts should be reasonable (within data range)
	for (double val : forecast.primary()) {
		REQUIRE(std::isfinite(val));
	}
}

TEST_CASE("SARIMA pure seasonal AR model", "[models][sarima][seasonal_ar]") {
	const std::vector<double> data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(0,0,0)(1,0,0)[12] - pure seasonal AR(1)
	auto model = ARIMABuilder()
		.withAR(0).withMA(0)
		.withSeasonalAR(1).withSeasonalDifferencing(0).withSeasonalMA(0)
		.withSeasonalPeriod(12)
		.withIntercept(true)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->seasonalARCoefficients().size() == 1);
	REQUIRE(model->seasonalPeriod() == 12);
	
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("SARIMA pure seasonal MA model", "[models][sarima][seasonal_ma]") {
	const std::vector<double> data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(0,0,0)(0,0,1)[12] - pure seasonal MA(1)
	auto model = ARIMABuilder()
		.withAR(1).withMA(0)  // Need AR for model validity
		.withSeasonalAR(0).withSeasonalDifferencing(0).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.withIntercept(true)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->seasonalMACoefficients().size() == 1);
	
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("SARIMA combined non-seasonal and seasonal AR", "[models][sarima][combined_ar]") {
	const std::vector<double> data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,0,0)(1,0,0)[12]
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(0).withMA(0)
		.withSeasonalAR(1).withSeasonalDifferencing(0).withSeasonalMA(0)
		.withSeasonalPeriod(12)
		.withIntercept(true)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->arCoefficients().size() == 1);
	REQUIRE(model->seasonalARCoefficients().size() == 1);
	
	const auto forecast = model->predict(24);
	REQUIRE(forecast.primary().size() == 24);
}

TEST_CASE("SARIMA full model with all components", "[models][sarima][full_model]") {
	const std::vector<double> data = generateSeasonalData(12, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,1,1)(1,1,1)[12] - full seasonal model
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(1).withMA(1)
		.withSeasonalAR(1).withSeasonalDifferencing(1).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.withIntercept(true)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	
	REQUIRE(model->arCoefficients().size() == 1);
	REQUIRE(model->maCoefficients().size() == 1);
	REQUIRE(model->seasonalARCoefficients().size() == 1);
	REQUIRE(model->seasonalMACoefficients().size() == 1);
	
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// All values should be finite
	for (double val : forecast.primary()) {
		REQUIRE(std::isfinite(val));
	}
}

TEST_CASE("SARIMA quarterly seasonality", "[models][sarima][quarterly]") {
	const std::vector<double> data = generateQuarterlySeasonal(20);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,0,1)(1,1,0)[4] - quarterly model
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(0).withMA(1)
		.withSeasonalAR(1).withSeasonalDifferencing(1).withSeasonalMA(0)
		.withSeasonalPeriod(4)
		.withIntercept(true)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->seasonalPeriod() == 4);
	
	const auto forecast = model->predict(8);
	REQUIRE(forecast.primary().size() == 8);
}

TEST_CASE("SARIMA handles AirPassengers data", "[models][sarima][airpassengers]") {
	const auto data = airPassengersData();
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// Classic AirPassengers model: SARIMA(0,1,1)(0,1,1)[12]
	auto model = ARIMABuilder()
		.withAR(0).withDifferencing(1).withMA(1)
		.withSeasonalAR(0).withSeasonalDifferencing(1).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.withIntercept(false)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// Forecasts should be in reasonable range (passengers increase over time)
	REQUIRE(forecast.primary()[0] > 150.0);
	REQUIRE(forecast.primary()[0] < 500.0);  // Allow for growth trend
	
	// Should have some seasonality (not all values identical)
	const double first = forecast.primary()[0];
	bool has_variation = false;
	for (std::size_t i = 1; i < forecast.primary().size(); ++i) {
		if (std::abs(forecast.primary()[i] - first) > 5.0) {
			has_variation = true;
			break;
		}
	}
	REQUIRE(has_variation);
}

TEST_CASE("SARIMA requires sufficient data for seasonal lags", "[models][sarima][validation]") {
	// Too little data for seasonal period 12
	const std::vector<double> short_data = {1., 2., 3., 4., 5., 6., 7., 8., 9., 10.};
	auto ts_short = tests::helpers::makeUnivariateSeries(short_data);
	
	auto model = ARIMABuilder()
		.withAR(1).withMA(1)
		.withSeasonalAR(1).withSeasonalPeriod(12)
		.build();
	
	REQUIRE_THROWS_AS(model->fit(ts_short), std::invalid_argument);
}

TEST_CASE("SARIMA with period=1 behaves like non-seasonal", "[models][sarima][edge_case]") {
	const std::vector<double> data = {10., 12., 15., 14., 16., 18., 20., 19., 22., 24.,
	                                   26., 25., 28., 30., 32., 31., 34., 36., 38., 37.};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA with s=1 should work like non-seasonal
	auto model_seasonal = ARIMABuilder()
		.withAR(1).withDifferencing(1).withMA(1)
		.withSeasonalAR(0).withSeasonalDifferencing(0).withSeasonalMA(0)
		.withSeasonalPeriod(1)
		.build();
	
	REQUIRE_NOTHROW(model_seasonal->fit(ts));
	auto forecast = model_seasonal->predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

TEST_CASE("SARIMA confidence intervals work with seasonal models", "[models][sarima][confidence]") {
	const std::vector<double> data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(1).withMA(1)
		.withSeasonalAR(1).withSeasonalDifferencing(1).withSeasonalMA(0)
		.withSeasonalPeriod(12)
		.build();
	
	model->fit(ts);
	
	const int horizon = 12;
	auto forecast = model->predictWithConfidence(horizon, 0.95);
	
	REQUIRE(forecast.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(forecast.lowerSeries().size() == static_cast<std::size_t>(horizon));
	REQUIRE(forecast.upperSeries().size() == static_cast<std::size_t>(horizon));
	
	// Confidence intervals should bracket forecasts
	for (std::size_t i = 0; i < static_cast<std::size_t>(horizon); ++i) {
		REQUIRE(forecast.lowerSeries()[i] <= forecast.primary()[i]);
		REQUIRE(forecast.upperSeries()[i] >= forecast.primary()[i]);
	}
}

TEST_CASE("SARIMA higher seasonal orders", "[models][sarima][high_order]") {
	const std::vector<double> data = generateSeasonalData(15, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,0,1)(2,0,2)[12] - higher seasonal orders
	auto model = ARIMABuilder()
		.withAR(1).withMA(1)
		.withSeasonalAR(2).withSeasonalMA(2)
		.withSeasonalPeriod(12)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->seasonalARCoefficients().size() == 2);
	REQUIRE(model->seasonalMACoefficients().size() == 2);
	
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("SARIMA weekly seasonality", "[models][sarima][weekly]") {
	const std::vector<double> data = generateSeasonalData(15, 7);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,0,1)(1,0,1)[7] - weekly pattern
	auto model = ARIMABuilder()
		.withAR(1).withMA(1)
		.withSeasonalAR(1).withSeasonalMA(1)
		.withSeasonalPeriod(7)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->seasonalPeriod() == 7);
	
	const auto forecast = model->predict(14);
	REQUIRE(forecast.primary().size() == 14);
}

TEST_CASE("SARIMA with only seasonal differencing", "[models][sarima][seasonal_diff_only]") {
	const std::vector<double> data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,0,1)(0,1,0)[12]
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(0).withMA(1)
		.withSeasonalAR(0).withSeasonalDifferencing(1).withSeasonalMA(0)
		.withSeasonalPeriod(12)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("SARIMA combined differencing", "[models][sarima][combined_diff]") {
	const std::vector<double> data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,1,1)(1,1,1)[12] - both types of differencing
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(1).withMA(1)
		.withSeasonalAR(1).withSeasonalDifferencing(1).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	
	// Should have AIC/BIC
	REQUIRE(model->aic().has_value());
	REQUIRE(model->bic().has_value());
	
	const auto forecast = model->predict(24);
	REQUIRE(forecast.primary().size() == 24);
}

TEST_CASE("SARIMA seasonal MA with no non-seasonal MA", "[models][sarima][seasonal_ma_only]") {
	const std::vector<double> data = generateSeasonalData(10, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(1,0,0)(0,0,1)[4]
	auto model = ARIMABuilder()
		.withAR(1).withMA(0)
		.withSeasonalAR(0).withSeasonalMA(1)
		.withSeasonalPeriod(4)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->maCoefficients().size() == 0);
	REQUIRE(model->seasonalMACoefficients().size() == 1);
	
	const auto forecast = model->predict(8);
	REQUIRE(forecast.primary().size() == 8);
}

TEST_CASE("SARIMA rejects invalid seasonal parameters", "[models][sarima][validation]") {
	// Negative seasonal orders
	REQUIRE_THROWS_AS(
		ARIMABuilder()
			.withSeasonalAR(-1)
			.withSeasonalPeriod(12)
			.build(),
		std::invalid_argument
	);
	
	REQUIRE_THROWS_AS(
		ARIMABuilder()
			.withSeasonalMA(1)
			.withSeasonalPeriod(12)
			.withSeasonalDifferencing(-1)
			.build(),
		std::invalid_argument
	);
}

TEST_CASE("SARIMA forecasts maintain seasonality", "[models][sarima][seasonality_preservation]") {
	// Generate data with strong seasonal component
	std::vector<double> data;
	for (int i = 0; i < 60; ++i) {
		double seasonal = 20.0 * std::sin(2.0 * M_PI * static_cast<double>(i % 12) / 12.0);
		data.push_back(100.0 + seasonal);
	}
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	auto model = ARIMABuilder()
		.withAR(0).withDifferencing(0).withMA(1)
		.withSeasonalAR(1).withSeasonalDifferencing(1).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.build();
	
	model->fit(ts);
	const auto forecast = model->predict(12);
	
	// Check that forecast has some variation (captures seasonality)
	double min_val = forecast.primary()[0];
	double max_val = forecast.primary()[0];
	for (const double val : forecast.primary()) {
		min_val = std::min(min_val, val);
		max_val = std::max(max_val, val);
	}
	
	// Should have reasonable seasonal variation
	REQUIRE(max_val - min_val > 10.0);
}

TEST_CASE("SARIMA residuals are available after fit", "[models][sarima][residuals]") {
	const std::vector<double> data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	auto model = ARIMABuilder()
		.withAR(1).withDifferencing(1).withMA(1)
		.withSeasonalAR(1).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.build();
	
	model->fit(ts);
	
	const auto &residuals = model->residuals();
	REQUIRE(!residuals.empty());
	
	// Residuals should have reasonable properties
	double sum = std::accumulate(residuals.begin(), residuals.end(), 0.0);
	double mean_residual = sum / static_cast<double>(residuals.size());
	
	// Mean of residuals should be reasonably small (allow for trend/seasonality)
	REQUIRE(std::abs(mean_residual) < 20.0);
}

TEST_CASE("SARIMA with multiple seasonal periods", "[models][sarima][multi_seasonal]") {
	// Test with period 7 (weekly)
	const std::vector<double> data = generateSeasonalData(20, 7);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	auto model = ARIMABuilder()
		.withAR(1).withMA(1)
		.withSeasonalAR(1).withSeasonalMA(1)
		.withSeasonalPeriod(7)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->seasonalPeriod() == 7);
	
	const auto forecast = model->predict(14);
	REQUIRE(forecast.primary().size() == 14);
}

TEST_CASE("SARIMA integration reverses differencing", "[models][sarima][integration]") {
	// Simple test: constant values should be reconstructed after differencing
	std::vector<double> constant(50, 42.0);
	
	// Apply seasonal differencing manually
	auto diff_result = anofoxtime::models::ARIMA::seasonalDifference(constant, 1, 12);
	
	// All differences should be zero (constant data)
	for (double val : diff_result) {
		REQUIRE(val == Catch::Approx(0.0).margin(1e-10));
	}
	
	// Integration should reconstruct (approximately)
	auto integrated = anofoxtime::models::ARIMA::seasonalIntegrate(diff_result, constant, 1, 12);
	
	// Should recover constant values for the forecasted portion
	for (double val : integrated) {
		REQUIRE(val == Catch::Approx(42.0).margin(1e-8));
	}
}

TEST_CASE("SARIMA with high seasonal AR order", "[models][sarima][high_seasonal_ar]") {
	const std::vector<double> data = generateSeasonalData(20, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// SARIMA(0,0,1)(2,1,0)[12]
	auto model = ARIMABuilder()
		.withAR(0).withMA(1)
		.withSeasonalAR(2).withSeasonalDifferencing(1).withSeasonalMA(0)
		.withSeasonalPeriod(12)
		.build();
	
	REQUIRE_NOTHROW(model->fit(ts));
	REQUIRE(model->seasonalARCoefficients().size() == 2);
	
	const auto forecast = model->predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("SARIMA coefficients are bounded", "[models][sarima][coefficients]") {
	const std::vector<double> data = generateSeasonalData(12, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	auto model = ARIMABuilder()
		.withAR(1).withMA(1)
		.withSeasonalAR(1).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.build();
	
	model->fit(ts);
	
	// MA coefficients should be bounded (for stability)
	const auto &ma_coeffs = model->maCoefficients();
	const auto &seasonal_ma_coeffs = model->seasonalMACoefficients();
	
	for (int i = 0; i < ma_coeffs.size(); ++i) {
		REQUIRE(std::abs(ma_coeffs[i]) < 1.0);
	}
	
	for (int i = 0; i < seasonal_ma_coeffs.size(); ++i) {
		REQUIRE(std::abs(seasonal_ma_coeffs[i]) < 1.0);
	}
}

TEST_CASE("SARIMA getName returns correct identifier", "[models][sarima][metadata]") {
	auto model = ARIMABuilder()
		.withAR(1).withMA(1)
		.withSeasonalAR(1).withSeasonalMA(1)
		.withSeasonalPeriod(12)
		.build();
	
	REQUIRE(model->getName() == "ARIMA");
}

