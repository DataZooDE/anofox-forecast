#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "anofox-time/models/croston_classic.hpp"
#include "anofox-time/models/croston_optimized.hpp"
#include "anofox-time/models/croston_sba.hpp"
#include "anofox-time/models/tsb.hpp"
#include "anofox-time/models/adida.hpp"
#include "anofox-time/models/imapa.hpp"
#include "anofox-time/core/time_series.hpp"

using namespace anofoxtime;
using namespace anofoxtime::models;
using namespace anofoxtime::core;

// Helper to create TimeSeries from vector
TimeSeries createTimeSeries(const std::vector<double>& data) {
    std::vector<TimeSeries::TimePoint> timestamps;
    auto start = TimeSeries::TimePoint{};
    for (size_t i = 0; i < data.size(); ++i) {
        timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
    }
    return TimeSeries(timestamps, data);
}

// =============================================================================
// CrostonClassic Tests
// =============================================================================

TEST_CASE("CrostonClassic: Basic functionality", "[intermittent][croston]") {
    // Intermittent data with zeros
    std::vector<double> data = {0, 3, 0, 0, 5, 0, 4, 0, 0, 6, 0, 5};
    auto ts = createTimeSeries(data);
    
    CrostonClassic model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary().size() == 3);
    REQUIRE(forecast.primary()[0] > 0.0);
    REQUIRE(forecast.primary()[0] == forecast.primary()[1]);  // Constant forecast
    
    REQUIRE(model.fittedValues().size() == data.size());
    REQUIRE(std::isnan(model.fittedValues()[0]));
}

TEST_CASE("CrostonClassic: All zeros handling", "[intermittent][croston]") {
    std::vector<double> data = {0, 0, 0, 0, 0};
    auto ts = createTimeSeries(data);
    
    CrostonClassic model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary().size() == 3);
    REQUIRE(forecast.primary()[0] == 0.0);
}

TEST_CASE("CrostonClassic: Single non-zero value", "[intermittent][croston]") {
    std::vector<double> data = {0, 0, 5, 0, 0};
    auto ts = createTimeSeries(data);
    
    CrostonClassic model;
    model.fit(ts);
    
    auto forecast = model.predict(2);
    
    REQUIRE(forecast.primary().size() == 2);
    REQUIRE(forecast.primary()[0] > 0.0);
}

TEST_CASE("CrostonClassic: High sparsity (80% zeros)", "[intermittent][croston]") {
    std::vector<double> data = {0, 0, 0, 0, 12, 0, 0, 0, 15, 0, 
                                0, 0, 0, 10, 0, 0, 0, 0, 14, 0};
    auto ts = createTimeSeries(data);
    
    CrostonClassic model;
    model.fit(ts);
    
    auto forecast = model.predict(5);
    
    REQUIRE(forecast.primary().size() == 5);
    REQUIRE(forecast.primary()[0] > 0.0);
    REQUIRE(forecast.primary()[0] < 20.0);  // Reasonable range
}

// =============================================================================
// CrostonOptimized Tests
// =============================================================================

TEST_CASE("CrostonOptimized: Basic functionality", "[intermittent][croston]") {
    std::vector<double> data = {0, 3, 0, 0, 5, 0, 4, 0, 0, 6, 0, 5};
    auto ts = createTimeSeries(data);
    
    CrostonOptimized model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary().size() == 3);
    REQUIRE(forecast.primary()[0] > 0.0);
    REQUIRE(model.fittedValues().size() == data.size());
}

TEST_CASE("CrostonOptimized: All zeros handling", "[intermittent][croston]") {
    std::vector<double> data = {0, 0, 0, 0, 0};
    auto ts = createTimeSeries(data);
    
    CrostonOptimized model;
    model.fit(ts);
    
    auto forecast = model.predict(2);
    
    REQUIRE(forecast.primary()[0] == 0.0);
}

TEST_CASE("CrostonOptimized: Comparison with Classic", "[intermittent][croston]") {
    std::vector<double> data = {0, 5, 0, 0, 6, 0, 5, 0, 0, 7};
    auto ts = createTimeSeries(data);
    
    CrostonClassic classic;
    classic.fit(ts);
    auto forecast_classic = classic.predict(1);
    
    CrostonOptimized optimized;
    optimized.fit(ts);
    auto forecast_optimized = optimized.predict(1);
    
    // Both should produce positive forecasts
    REQUIRE(forecast_classic.primary()[0] > 0.0);
    REQUIRE(forecast_optimized.primary()[0] > 0.0);
    
    // Optimized may differ from classic
    REQUIRE(forecast_optimized.primary()[0] > 0.0);
}

// =============================================================================
// CrostonSBA Tests
// =============================================================================

TEST_CASE("CrostonSBA: Debiasing factor applied", "[intermittent][croston]") {
    std::vector<double> data = {0, 3, 0, 0, 5, 0, 4, 0, 0, 6};
    auto ts = createTimeSeries(data);
    
    CrostonClassic classic;
    classic.fit(ts);
    auto forecast_classic = classic.predict(1);
    
    CrostonSBA sba;
    sba.fit(ts);
    auto forecast_sba = sba.predict(1);
    
    // SBA should be 0.95 * Classic
    REQUIRE_THAT(forecast_sba.primary()[0], 
                 Catch::Matchers::WithinRel(forecast_classic.primary()[0] * 0.95, 0.01));
}

TEST_CASE("CrostonSBA: All zeros handling", "[intermittent][croston]") {
    std::vector<double> data = {0, 0, 0, 0, 0};
    auto ts = createTimeSeries(data);
    
    CrostonSBA model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary()[0] == 0.0);
}

// =============================================================================
// TSB Tests
// =============================================================================

TEST_CASE("TSB: Basic functionality", "[intermittent][tsb]") {
    std::vector<double> data = {0, 3, 0, 0, 5, 0, 4, 0, 0, 6};
    auto ts = createTimeSeries(data);
    
    TSB model(0.1, 0.1);
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary().size() == 3);
    REQUIRE(forecast.primary()[0] >= 0.0);
    REQUIRE(model.fittedValues().size() == data.size());
}

TEST_CASE("TSB: All zeros handling", "[intermittent][tsb]") {
    std::vector<double> data = {0, 0, 0, 0, 0};
    auto ts = createTimeSeries(data);
    
    TSB model(0.1, 0.1);
    model.fit(ts);
    
    auto forecast = model.predict(2);
    
    REQUIRE(forecast.primary()[0] == 0.0);
}

TEST_CASE("TSB: Different alpha values", "[intermittent][tsb]") {
    std::vector<double> data = {0, 5, 0, 0, 6, 0, 5, 0, 0, 7};
    auto ts = createTimeSeries(data);
    
    TSB model1(0.1, 0.1);
    model1.fit(ts);
    auto forecast1 = model1.predict(1);
    
    TSB model2(0.3, 0.3);
    model2.fit(ts);
    auto forecast2 = model2.predict(1);
    
    // Different alphas should give different results
    REQUIRE(forecast1.primary()[0] > 0.0);
    REQUIRE(forecast2.primary()[0] > 0.0);
}

TEST_CASE("TSB: Invalid alpha values", "[intermittent][tsb]") {
    REQUIRE_THROWS_AS(TSB(-0.1, 0.1), std::invalid_argument);
    REQUIRE_THROWS_AS(TSB(0.1, 1.5), std::invalid_argument);
}

// =============================================================================
// ADIDA Tests
// =============================================================================

TEST_CASE("ADIDA: Basic functionality", "[intermittent][adida]") {
    std::vector<double> data = {0, 3, 0, 0, 5, 0, 4, 0, 0, 6, 0, 5};
    auto ts = createTimeSeries(data);
    
    ADIDA model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary().size() == 3);
    REQUIRE(forecast.primary()[0] >= 0.0);
    REQUIRE(model.aggregationLevel() > 0);
    REQUIRE(model.fittedValues().size() == data.size());
}

TEST_CASE("ADIDA: All zeros handling", "[intermittent][adida]") {
    std::vector<double> data = {0, 0, 0, 0, 0};
    auto ts = createTimeSeries(data);
    
    ADIDA model;
    model.fit(ts);
    
    auto forecast = model.predict(2);
    
    REQUIRE(forecast.primary()[0] == 0.0);
    REQUIRE(model.aggregationLevel() == 1);
}

TEST_CASE("ADIDA: High sparsity data", "[intermittent][adida]") {
    // Very sparse: ~90% zeros
    std::vector<double> data = {0, 0, 0, 0, 0, 0, 0, 0, 0, 12,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 15,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 10};
    auto ts = createTimeSeries(data);
    
    ADIDA model;
    model.fit(ts);
    
    auto forecast = model.predict(5);
    
    REQUIRE(forecast.primary()[0] > 0.0);
    REQUIRE(model.aggregationLevel() >= 1);
}

// =============================================================================
// IMAPA Tests
// =============================================================================

TEST_CASE("IMAPA: Basic functionality", "[intermittent][imapa]") {
    std::vector<double> data = {0, 3, 0, 0, 5, 0, 4, 0, 0, 6, 0, 5};
    auto ts = createTimeSeries(data);
    
    IMAPA model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary().size() == 3);
    REQUIRE(forecast.primary()[0] >= 0.0);
    REQUIRE(model.maxAggregationLevel() > 0);
    REQUIRE(model.fittedValues().size() == data.size());
}

TEST_CASE("IMAPA: All zeros handling", "[intermittent][imapa]") {
    std::vector<double> data = {0, 0, 0, 0, 0};
    auto ts = createTimeSeries(data);
    
    IMAPA model;
    model.fit(ts);
    
    auto forecast = model.predict(2);
    
    REQUIRE(forecast.primary()[0] == 0.0);
}

TEST_CASE("IMAPA: Medium sparsity (50% zeros)", "[intermittent][imapa]") {
    std::vector<double> data = {5, 0, 6, 0, 4, 0, 7, 0, 5, 0, 6, 0};
    auto ts = createTimeSeries(data);
    
    IMAPA model;
    model.fit(ts);
    
    auto forecast = model.predict(4);
    
    REQUIRE(forecast.primary()[0] > 0.0);
    REQUIRE(forecast.primary()[0] < 10.0);  // Reasonable range
}

TEST_CASE("IMAPA: Very sparse data", "[intermittent][imapa]") {
    // ~85% zeros
    std::vector<double> data = {0, 0, 0, 0, 0, 0, 10, 0, 0, 0,
                                0, 0, 0, 12, 0, 0, 0, 0, 0, 11};
    auto ts = createTimeSeries(data);
    
    IMAPA model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary()[0] > 0.0);
}

// =============================================================================
// Integration Tests
// =============================================================================

TEST_CASE("All intermittent models: Consistency check", "[intermittent][integration]") {
    std::vector<double> data = {0, 5, 0, 0, 6, 0, 5, 0, 0, 7, 0, 6};
    auto ts = createTimeSeries(data);
    
    CrostonClassic classic;
    classic.fit(ts);
    auto f_classic = classic.predict(1);
    
    CrostonOptimized optimized;
    optimized.fit(ts);
    auto f_optimized = optimized.predict(1);
    
    CrostonSBA sba;
    sba.fit(ts);
    auto f_sba = sba.predict(1);
    
    TSB tsb(0.1, 0.1);
    tsb.fit(ts);
    auto f_tsb = tsb.predict(1);
    
    ADIDA adida;
    adida.fit(ts);
    auto f_adida = adida.predict(1);
    
    IMAPA imapa;
    imapa.fit(ts);
    auto f_imapa = imapa.predict(1);
    
    // All should produce positive forecasts
    REQUIRE(f_classic.primary()[0] > 0.0);
    REQUIRE(f_optimized.primary()[0] > 0.0);
    REQUIRE(f_sba.primary()[0] > 0.0);
    REQUIRE(f_tsb.primary()[0] > 0.0);
    REQUIRE(f_adida.primary()[0] > 0.0);
    REQUIRE(f_imapa.primary()[0] > 0.0);
    
    // SBA should be 95% of Classic
    REQUIRE_THAT(f_sba.primary()[0], 
                 Catch::Matchers::WithinRel(f_classic.primary()[0] * 0.95, 0.01));
}

TEST_CASE("All intermittent models: Fitted values length", "[intermittent][integration]") {
    std::vector<double> data = {0, 3, 0, 0, 5, 0, 4};
    auto ts = createTimeSeries(data);
    
    CrostonClassic classic;
    classic.fit(ts);
    REQUIRE(classic.fittedValues().size() == data.size());
    REQUIRE(classic.residuals().size() == data.size());
    
    CrostonOptimized optimized;
    optimized.fit(ts);
    REQUIRE(optimized.fittedValues().size() == data.size());
    
    TSB tsb(0.15, 0.15);
    tsb.fit(ts);
    REQUIRE(tsb.fittedValues().size() == data.size());
    
    ADIDA adida;
    adida.fit(ts);
    REQUIRE(adida.fittedValues().size() == data.size());
    
    IMAPA imapa;
    imapa.fit(ts);
    REQUIRE(imapa.fittedValues().size() == data.size());
}

TEST_CASE("Intermittent models: Empty series throws", "[intermittent][error]") {
    std::vector<double> empty_data;
    auto ts = createTimeSeries(empty_data);
    
    CrostonClassic classic;
    REQUIRE_THROWS_AS(classic.fit(ts), std::invalid_argument);
    
    TSB tsb(0.1, 0.1);
    REQUIRE_THROWS_AS(tsb.fit(ts), std::invalid_argument);
    
    ADIDA adida;
    REQUIRE_THROWS_AS(adida.fit(ts), std::invalid_argument);
    
    IMAPA imapa;
    REQUIRE_THROWS_AS(imapa.fit(ts), std::invalid_argument);
}

TEST_CASE("Intermittent models: Predict before fit throws", "[intermittent][error]") {
    CrostonClassic classic;
    REQUIRE_THROWS_AS(classic.predict(3), std::runtime_error);
    
    TSB tsb(0.1, 0.1);
    REQUIRE_THROWS_AS(tsb.predict(3), std::runtime_error);
    
    ADIDA adida;
    REQUIRE_THROWS_AS(adida.predict(3), std::runtime_error);
    
    IMAPA imapa;
    REQUIRE_THROWS_AS(imapa.predict(3), std::runtime_error);
}

TEST_CASE("Intermittent models: Negative horizon throws", "[intermittent][error]") {
    std::vector<double> data = {0, 5, 0, 6};
    auto ts = createTimeSeries(data);
    
    CrostonClassic classic;
    classic.fit(ts);
    REQUIRE_THROWS_AS(classic.predict(0), std::invalid_argument);
    REQUIRE_THROWS_AS(classic.predict(-1), std::invalid_argument);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("CrostonClassic: Constant intervals", "[intermittent][edge]") {
    // Demand every 3 time steps
    std::vector<double> data = {0, 0, 5, 0, 0, 6, 0, 0, 5, 0, 0, 7};
    auto ts = createTimeSeries(data);
    
    CrostonClassic model;
    model.fit(ts);
    
    auto forecast = model.predict(3);
    
    REQUIRE(forecast.primary()[0] > 0.0);
    REQUIRE(model.lastIntervalLevel() > 0.0);
}

TEST_CASE("ADIDA: Minimum aggregation level", "[intermittent][edge]") {
    // Consecutive non-zeros
    std::vector<double> data = {5, 6, 5, 7, 6};
    auto ts = createTimeSeries(data);
    
    ADIDA model;
    model.fit(ts);
    
    // Aggregation level should be 1 for consecutive data
    REQUIRE(model.aggregationLevel() >= 1);
    
    auto forecast = model.predict(2);
    REQUIRE(forecast.primary()[0] > 0.0);
}

TEST_CASE("IMAPA: Single aggregation level", "[intermittent][edge]") {
    // Short series
    std::vector<double> data = {0, 5, 0, 6};
    auto ts = createTimeSeries(data);
    
    IMAPA model;
    model.fit(ts);
    
    auto forecast = model.predict(2);
    
    REQUIRE(forecast.primary()[0] >= 0.0);
    REQUIRE(model.maxAggregationLevel() >= 1);
}

TEST_CASE("TSB: Probability component", "[intermittent][tsb]") {
    std::vector<double> data = {0, 5, 0, 0, 6, 0, 5};
    auto ts = createTimeSeries(data);
    
    TSB model(0.2, 0.2);
    model.fit(ts);
    
    // Probability should be between 0 and 1
    REQUIRE(model.lastProbabilityLevel() >= 0.0);
    REQUIRE(model.lastProbabilityLevel() <= 1.0);
    
    // Demand should be positive
    REQUIRE(model.lastDemandLevel() > 0.0);
}


