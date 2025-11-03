#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include <iostream>
#include <vector>
#include <cmath>

int main() {
    using namespace anofoxtime;
    using TimePoint = std::chrono::system_clock::time_point;

    // Create simple test data (trend + seasonality)
    std::vector<double> data;
    std::vector<TimePoint> timestamps;
    auto base_time = std::chrono::system_clock::now();

    for (int i = 0; i < 100; ++i) {
        double trend = 0.5 * i;
        double seasonal = 10 * std::sin(2 * 3.14159265358979 * i / 12);
        data.push_back(trend + seasonal + 100);
        timestamps.push_back(base_time + std::chrono::hours(24 * i));  // Daily data
    }

    std::cout << "Test data created: " << data.size() << " points" << std::endl;
    std::cout << "First 5 values: ";
    for (int i = 0; i < 5; ++i) {
        std::cout << data[i] << " ";
    }
    std::cout << std::endl;

    // Create time series with timestamps
    core::TimeSeries ts(timestamps, data);
    std::cout << "TimeSeries created" << std::endl;

    // Create MFLES with defaults
    models::MFLES::Params params;
    params.seasonal_periods = {12};
    params.max_rounds = 10;

    std::cout << "Creating MFLES model with params:" << std::endl;
    std::cout << "  - Seasonal periods: " << params.seasonal_periods[0] << std::endl;
    std::cout << "  - Max rounds: " << params.max_rounds << std::endl;

    models::MFLES model(params);

    // Fit
    std::cout << "\n=== Fitting MFLES ===" << std::endl;
    try {
        model.fit(ts);
        std::cout << "Fit complete!" << std::endl;
        std::cout << "  - Rounds used: " << model.actualRoundsUsed() << std::endl;
        std::cout << "  - Is multiplicative: " << (model.isMultiplicative() ? "yes" : "no") << std::endl;

        // Check fitted values
        const auto& fitted = model.fittedValues();
        std::cout << "  - Fitted values: " << fitted.size() << " points" << std::endl;
        if (!fitted.empty()) {
            std::cout << "  - First 5 fitted: ";
            for (int i = 0; i < std::min(5, (int)fitted.size()); ++i) {
                std::cout << fitted[i] << " ";
            }
            std::cout << std::endl;
        }

        // Check residuals
        const auto& residuals = model.residuals();
        std::cout << "  - Residuals: " << residuals.size() << " points" << std::endl;
        if (!residuals.empty()) {
            double mean_resid = 0.0;
            for (const auto& r : residuals) {
                mean_resid += r;
            }
            mean_resid /= residuals.size();
            std::cout << "  - Mean residual: " << mean_resid << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR during fit: " << e.what() << std::endl;
        return 1;
    }

    // Predict
    std::cout << "\n=== Forecasting ===" << std::endl;
    try {
        auto forecast = model.predict(12);
        const auto& forecasts = forecast.primary();
        std::cout << "Forecast generated: " << forecasts.size() << " points" << std::endl;

        if (!forecasts.empty()) {
            std::cout << "Forecast values: ";
            for (const auto& val : forecasts) {
                std::cout << val << " ";
            }
            std::cout << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR during predict: " << e.what() << std::endl;
        return 1;
    }

    // Decompose
    std::cout << "\n=== Decomposition ===" << std::endl;
    try {
        auto decomp = model.seasonal_decompose();
        std::cout << "Decomposition extracted:" << std::endl;
        std::cout << "  - Trend: " << decomp.trend.size() << " points" << std::endl;
        std::cout << "  - Seasonal: " << decomp.seasonal.size() << " points" << std::endl;
        std::cout << "  - Level: " << decomp.level.size() << " points" << std::endl;
        std::cout << "  - Residuals: " << decomp.residuals.size() << " points" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR during decompose: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n=== All tests passed! ===" << std::endl;
    return 0;
}
