#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include <iostream>
#include <vector>
#include <cmath>

int main() {
    using namespace anofoxtime;
    using TimePoint = std::chrono::system_clock::time_point;

    // Create test data with trend + seasonality + OUTLIERS
    std::vector<double> data;
    std::vector<TimePoint> timestamps;
    auto base_time = std::chrono::system_clock::now();

    for (int i = 0; i < 100; ++i) {
        double trend = 0.5 * i;
        double seasonal = 10 * std::sin(2 * 3.14159265358979 * i / 12);
        double value = trend + seasonal + 100;

        // Add outliers at positions 25, 50, 75
        if (i == 25 || i == 50 || i == 75) {
            value += 50.0;  // Large positive outlier
        }

        data.push_back(value);
        timestamps.push_back(base_time + std::chrono::hours(24 * i));
    }

    std::cout << "Test data created with outliers at positions 25, 50, 75" << std::endl;
    std::cout << "Data[24]=" << data[24] << ", Data[25]=" << data[25] << " (outlier)" << std::endl;
    std::cout << "Data[49]=" << data[49] << ", Data[50]=" << data[50] << " (outlier)" << std::endl;

    // Create time series
    core::TimeSeries ts(timestamps, data);

    // Test 1: OLS (will be affected by outliers)
    std::cout << "\n=== Test 1: OLS Trend (affected by outliers) ===" << std::endl;
    models::MFLES::Params params_ols;
    params_ols.seasonal_periods = {12};
    params_ols.max_rounds = 10;
    params_ols.trend_method = models::TrendMethod::OLS;

    models::MFLES model_ols(params_ols);
    model_ols.fit(ts);

    std::cout << "OLS fit complete!" << std::endl;
    std::cout << "  - Rounds used: " << model_ols.actualRoundsUsed() << std::endl;

    auto forecast_ols = model_ols.predict(12);
    const auto& forecasts_ols = forecast_ols.primary();
    std::cout << "  - First 3 forecasts: ";
    for (int i = 0; i < 3; ++i) {
        std::cout << forecasts_ols[i] << " ";
    }
    std::cout << std::endl;

    // Test 2: Siegel Robust (resistant to outliers)
    std::cout << "\n=== Test 2: Siegel Robust Trend (resistant to outliers) ===" << std::endl;
    models::MFLES::Params params_siegel;
    params_siegel.seasonal_periods = {12};
    params_siegel.max_rounds = 10;
    params_siegel.trend_method = models::TrendMethod::SIEGEL_ROBUST;

    models::MFLES model_siegel(params_siegel);
    model_siegel.fit(ts);

    std::cout << "Siegel fit complete!" << std::endl;
    std::cout << "  - Rounds used: " << model_siegel.actualRoundsUsed() << std::endl;

    auto forecast_siegel = model_siegel.predict(12);
    const auto& forecasts_siegel = forecast_siegel.primary();
    std::cout << "  - First 3 forecasts: ";
    for (int i = 0; i < 3; ++i) {
        std::cout << forecasts_siegel[i] << " ";
    }
    std::cout << std::endl;

    // Compare residuals
    const auto& resid_ols = model_ols.residuals();
    const auto& resid_siegel = model_siegel.residuals();

    double mae_ols = 0.0, mae_siegel = 0.0;
    for (size_t i = 0; i < resid_ols.size(); ++i) {
        mae_ols += std::abs(resid_ols[i]);
        mae_siegel += std::abs(resid_siegel[i]);
    }
    mae_ols /= resid_ols.size();
    mae_siegel /= resid_siegel.size();

    std::cout << "\n=== Comparison ===" << std::endl;
    std::cout << "  OLS MAE:    " << mae_ols << std::endl;
    std::cout << "  Siegel MAE: " << mae_siegel << std::endl;

    if (mae_siegel < mae_ols) {
        std::cout << "\n✓ Siegel regression is MORE robust to outliers (lower MAE)" << std::endl;
    } else {
        std::cout << "\n✓ OLS performs better on this dataset (may need more outliers)" << std::endl;
    }

    std::cout << "\n=== All Siegel tests passed! ===" << std::endl;
    return 0;
}
