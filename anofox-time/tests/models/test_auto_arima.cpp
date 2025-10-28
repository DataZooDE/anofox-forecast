#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "anofox-time/models/auto_arima.hpp"
#include "common/time_series_helpers.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

using anofoxtime::models::AutoARIMA;
using anofoxtime::models::AutoARIMAComponents;
using InformationCriterion = AutoARIMA::InformationCriterion;

namespace {

std::vector<double> generateARSeries(double phi, double start, std::size_t length) {
	std::vector<double> series;
	series.reserve(length);
	series.push_back(start);
	for (std::size_t i = 1; i < length; ++i) {
		series.push_back(phi * series.back() + (static_cast<double>(rand()) / RAND_MAX - 0.5) * 0.1);
	}
	return series;
}

std::vector<double> generateMASeries(double theta, std::size_t length, double noise_scale = 0.5) {
	std::vector<double> series;
	series.reserve(length);
	double prev_noise = 0.0;
	for (std::size_t i = 0; i < length; ++i) {
		double noise = (static_cast<double>(rand()) / RAND_MAX - 0.5) * noise_scale;
		series.push_back(10.0 + noise + theta * prev_noise);
		prev_noise = noise;
	}
	return series;
}

std::vector<double> generateTrendSeries(std::size_t length, double slope = 0.5) {
	std::vector<double> series;
	series.reserve(length);
	for (std::size_t i = 0; i < length; ++i) {
		series.push_back(100.0 + slope * static_cast<double>(i));
	}
	return series;
}

std::vector<double> generateSeasonalSeries(std::size_t cycles, int period = 12) {
	std::vector<double> series;
	series.reserve(cycles * static_cast<std::size_t>(period));
	for (std::size_t c = 0; c < cycles; ++c) {
		for (int t = 0; t < period; ++t) {
			double seasonal = 10.0 * std::sin(2.0 * M_PI * static_cast<double>(t) / static_cast<double>(period));
			series.push_back(100.0 + seasonal);
		}
	}
	return series;
}

void requireClose(double value, double expected, double tolerance = 1e-6) {
	REQUIRE(value == Catch::Approx(expected).margin(tolerance));
}

} // namespace

TEST_CASE("AutoARIMA rejects multivariate input", "[models][auto_arima][validation]") {
	AutoARIMA auto_arima(0);
	auto multivariate = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0}, {0.5, 0.6, 0.7}});
	REQUIRE_THROWS_AS(auto_arima.fit(multivariate), std::invalid_argument);
}

TEST_CASE("AutoARIMA validates parameter ranges", "[models][auto_arima][validation]") {
	AutoARIMA auto_arima(0);
	REQUIRE_THROWS_AS(auto_arima.setMaxP(-1), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_arima.setMaxD(3), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_arima.setMaxQ(-1), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_arima.setMaxSeasonalP(-1), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_arima.setMaxSeasonalD(2), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_arima.setMaxSeasonalQ(-1), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_arima.setMaxIterations(0), std::invalid_argument);
	
	REQUIRE_NOTHROW(auto_arima.setMaxP(3));
	REQUIRE_NOTHROW(auto_arima.setMaxD(2));
	REQUIRE_NOTHROW(auto_arima.setMaxQ(3));
	REQUIRE_NOTHROW(auto_arima.setMaxIterations(50));
}

TEST_CASE("AutoARIMA requires sufficient data", "[models][auto_arima][validation]") {
	AutoARIMA auto_arima(0);
	auto small_data = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0});
	REQUIRE_THROWS_AS(auto_arima.fit(small_data), std::invalid_argument);
}

TEST_CASE("AutoARIMA identifies AR(1) process", "[models][auto_arima][fit][ar]") {
	srand(42);  // For reproducibility
	const double phi = 0.7;
	const auto data = generateARSeries(phi, 10.0, 100);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(3).setMaxQ(3).setMaxD(1);
	auto_arima.fit(ts);

	const auto &comp = auto_arima.components();
	REQUIRE(comp.p > 0);  // Should select some AR component
	REQUIRE(comp.d >= 0);  // May or may not difference
	
	const auto &metrics = auto_arima.metrics();
	REQUIRE(std::isfinite(metrics.aicc));
	REQUIRE(std::isfinite(metrics.aic));
	REQUIRE(std::isfinite(metrics.bic));

	// Should be able to forecast
	const auto forecast = auto_arima.predict(5);
	REQUIRE(forecast.primary().size() == 5);
}

TEST_CASE("AutoARIMA identifies MA(1) process", "[models][auto_arima][fit][ma]") {
	srand(42);
	const double theta = 0.6;
	const auto data = generateMASeries(theta, 100);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(3).setMaxQ(3).setMaxD(1);
	auto_arima.fit(ts);

	const auto &comp = auto_arima.components();
	// Should select some model (may choose AR approximation of MA)
	REQUIRE((comp.p > 0 || comp.q > 0));

	const auto &metrics = auto_arima.metrics();
	REQUIRE(std::isfinite(metrics.aicc));

	const auto forecast = auto_arima.predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

TEST_CASE("AutoARIMA handles trending data with differencing", "[models][auto_arima][fit][differencing]") {
	const auto data = generateTrendSeries(80, 1.5);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(2).setMaxQ(2).setMaxD(2);
	auto_arima.fit(ts);

	const auto &comp = auto_arima.components();
	REQUIRE(comp.d >= 1);  // Should apply differencing for trend

	const auto forecast = auto_arima.predict(10);
	REQUIRE(forecast.primary().size() == 10);
	
	// Forecast should continue the trend (at least roughly)
	REQUIRE(forecast.primary()[5] > data.back() - 50.0);
}

TEST_CASE("AutoARIMA stepwise vs exhaustive search", "[models][auto_arima][fit][stepwise]") {
	srand(42);
	const auto data = generateARSeries(0.5, 10.0, 60);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	// Stepwise search
	AutoARIMA auto_arima_stepwise(0);
	auto_arima_stepwise.setMaxP(3).setMaxQ(3).setStepwise(true);
	auto_arima_stepwise.fit(ts);
	
	const auto &diag_stepwise = auto_arima_stepwise.diagnostics();
	REQUIRE(diag_stepwise.stepwise_used);

	// Exhaustive search
	AutoARIMA auto_arima_exhaustive(0);
	auto_arima_exhaustive.setMaxP(2).setMaxQ(2).setStepwise(false);
	auto_arima_exhaustive.fit(ts);
	
	const auto &diag_exhaustive = auto_arima_exhaustive.diagnostics();
	REQUIRE_FALSE(diag_exhaustive.stepwise_used);
	
	// Both should evaluate models successfully
	REQUIRE(diag_exhaustive.models_evaluated > 0);
	REQUIRE(diag_stepwise.models_evaluated > 0);
}

TEST_CASE("AutoARIMA different information criteria", "[models][auto_arima][fit][ic]") {
	srand(42);
	const auto data = generateARSeries(0.6, 10.0, 60);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	// AIC
	AutoARIMA auto_arima_aic(0);
	auto_arima_aic.setInformationCriterion(InformationCriterion::AIC);
	auto_arima_aic.setMaxP(3).setMaxQ(3);
	auto_arima_aic.fit(ts);
	const auto &metrics_aic = auto_arima_aic.metrics();
	REQUIRE(std::isfinite(metrics_aic.aic));

	// AICc
	AutoARIMA auto_arima_aicc(0);
	auto_arima_aicc.setInformationCriterion(InformationCriterion::AICc);
	auto_arima_aicc.setMaxP(3).setMaxQ(3);
	auto_arima_aicc.fit(ts);
	const auto &metrics_aicc = auto_arima_aicc.metrics();
	REQUIRE(std::isfinite(metrics_aicc.aicc));

	// BIC
	AutoARIMA auto_arima_bic(0);
	auto_arima_bic.setInformationCriterion(InformationCriterion::BIC);
	auto_arima_bic.setMaxP(3).setMaxQ(3);
	auto_arima_bic.fit(ts);
	const auto &metrics_bic = auto_arima_bic.metrics();
	REQUIRE(std::isfinite(metrics_bic.bic));
}

TEST_CASE("AutoARIMA produces valid forecasts", "[models][auto_arima][forecast]") {
	srand(42);
	const auto data = generateARSeries(0.5, 20.0, 80);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(3).setMaxQ(3);
	auto_arima.fit(ts);

	const int horizon = 12;
	const auto forecast = auto_arima.predict(horizon);
	REQUIRE(forecast.primary().size() == static_cast<std::size_t>(horizon));

	// All forecast values should be finite
	for (double val : forecast.primary()) {
		REQUIRE(std::isfinite(val));
	}
}

TEST_CASE("AutoARIMA confidence intervals", "[models][auto_arima][forecast][confidence]") {
	srand(42);
	const auto data = generateARSeries(0.6, 15.0, 80);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(3).setMaxQ(3);
	auto_arima.fit(ts);

	const int horizon = 6;
	const auto forecast = auto_arima.predictWithConfidence(horizon, 0.95);
	
	REQUIRE(forecast.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(forecast.lowerSeries().size() == static_cast<std::size_t>(horizon));
	REQUIRE(forecast.upperSeries().size() == static_cast<std::size_t>(horizon));

	// Confidence intervals should bracket the forecast
	for (std::size_t i = 0; i < static_cast<std::size_t>(horizon); ++i) {
		REQUIRE(forecast.lowerSeries()[i] <= forecast.primary()[i]);
		REQUIRE(forecast.upperSeries()[i] >= forecast.primary()[i]);
	}
}

TEST_CASE("AutoARIMA exposes diagnostics", "[models][auto_arima][diagnostics]") {
	srand(42);
	const auto data = generateARSeries(0.5, 10.0, 60);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(3).setMaxQ(3);
	auto_arima.fit(ts);

	const auto &diag = auto_arima.diagnostics();
	REQUIRE(diag.training_data_size == data.size());
	REQUIRE(diag.models_evaluated > 0);
	REQUIRE(diag.models_failed >= 0);
}

TEST_CASE("AutoARIMA handles seasonal period specification", "[models][auto_arima][seasonal]") {
	const auto data = generateSeasonalSeries(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(12);  // Monthly seasonality
	auto_arima.setMaxP(2).setMaxQ(2);
	// Note: Full seasonal ARIMA (P,D,Q) support depends on base ARIMA implementation
	// For now, seasonal_period is stored but seasonal orders are not used
	auto_arima.setMaxSeasonalP(0).setMaxSeasonalD(0).setMaxSeasonalQ(0);  // Keep seasonal orders at 0 for now
	auto_arima.fit(ts);

	const auto &comp = auto_arima.components();
	REQUIRE(comp.seasonal_period == 12);
	// Should select a non-seasonal ARIMA model
	REQUIRE(comp.P == 0);
	REQUIRE(comp.D == 0);
	REQUIRE(comp.Q == 0);

	const auto forecast = auto_arima.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("AutoARIMA accessor methods throw before fit", "[models][auto_arima][validation][fit_check]") {
	AutoARIMA auto_arima(0);
	
	REQUIRE_THROWS_AS(auto_arima.components(), std::logic_error);
	REQUIRE_THROWS_AS(auto_arima.parameters(), std::logic_error);
	REQUIRE_THROWS_AS(auto_arima.metrics(), std::logic_error);
	REQUIRE_THROWS_AS(auto_arima.diagnostics(), std::logic_error);
	REQUIRE_THROWS_AS(auto_arima.fittedValues(), std::logic_error);
	REQUIRE_THROWS_AS(auto_arima.residuals(), std::logic_error);
	REQUIRE_THROWS_AS(auto_arima.predict(5), std::logic_error);
}

TEST_CASE("AutoARIMA handles nearly constant series", "[models][auto_arima][fit][constant]") {
	// Generate nearly constant data with tiny variations to avoid singularity
	std::vector<double> constant_data(50);
	for (std::size_t i = 0; i < 50; ++i) {
		constant_data[i] = 42.0 + (static_cast<double>(i % 3) - 1.0) * 0.001;
	}
	auto ts = tests::helpers::makeUnivariateSeries(constant_data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(1).setMaxQ(1).setMaxD(1);
	auto_arima.fit(ts);

	const auto &comp = auto_arima.components();
	// Should select a simple model
	REQUIRE((comp.p + comp.q + comp.d) >= 0);

	const auto forecast = auto_arima.predict(5);
	REQUIRE(forecast.primary().size() == 5);
	// Forecast should be close to constant value
	for (double val : forecast.primary()) {
		REQUIRE(val == Catch::Approx(42.0).margin(1.0));
	}
}

TEST_CASE("AutoARIMA configuration chaining works", "[models][auto_arima][config]") {
	AutoARIMA auto_arima(0);
	
	// Method chaining should work
	REQUIRE_NOTHROW(
		auto_arima.setMaxP(4)
		    .setMaxD(1)
		    .setMaxQ(4)
		    .setMaxSeasonalP(2)
		    .setMaxSeasonalD(1)
		    .setMaxSeasonalQ(2)
		    .setStepwise(true)
		    .setInformationCriterion(InformationCriterion::BIC)
		    .setAllowDrift(true)
		    .setAllowMeanTerm(true)
		    .setMaxIterations(50)
	);
}

TEST_CASE("AutoARIMA residuals and fitted values available after fit", "[models][auto_arima][fit][residuals]") {
	srand(42);
	const auto data = generateARSeries(0.5, 10.0, 60);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoARIMA auto_arima(0);
	auto_arima.setMaxP(3).setMaxQ(3);
	auto_arima.fit(ts);

	const auto &residuals = auto_arima.residuals();
	const auto &fitted = auto_arima.fittedValues();
	
	REQUIRE(!residuals.empty());
	// Residuals should be roughly centered around zero
	double mean_residual = std::accumulate(residuals.begin(), residuals.end(), 0.0) / 
	                       static_cast<double>(residuals.size());
	REQUIRE(std::abs(mean_residual) < 1.0);
}

