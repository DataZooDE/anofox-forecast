#pragma once

#include "anofox-time/core/time_series.hpp"
#include "anofox-time/utils/logging.hpp"
#include <vector>
#include <cstddef>

namespace anofoxtime::seasonality {

class STLDecomposition {
public:
    class Builder {
    public:
        Builder& withPeriod(std::size_t period);
        Builder& withSeasonalSmoother(std::size_t window);
        Builder& withTrendSmoother(std::size_t window);
        Builder& withIterations(std::size_t iterations);
        Builder& withRobust(bool robust);
        STLDecomposition build() const;

    private:
        std::size_t period_ = 12;
        std::size_t seasonal_smoother_ = 7;
        std::size_t trend_smoother_ = 15;
        std::size_t iterations_ = 2;
        bool robust_ = false;
    };

    static Builder builder();

    explicit STLDecomposition(std::size_t seasonal_period,
                              std::size_t seasonal_smoother = 7,
                              std::size_t trend_smoother = 15,
                              std::size_t iterations = 2,
                              bool robust = false);

    // Explicitly use anofoxtime::core::TimeSeries
    void fit(const anofoxtime::core::TimeSeries& ts);

    const std::vector<double>& trend() const { return trend_; }
    const std::vector<double>& seasonal() const { return seasonal_; }
    const std::vector<double>& remainder() const { return remainder_; }

    double seasonalStrength() const;
    double trendStrength() const;

    std::size_t seasonalPeriod() const { return seasonal_period_; }

private:
    static double variance(const std::vector<double>& values);
    void applyLowessSmoothing(const std::vector<double>& x,
                             const std::vector<double>& y,
                             std::vector<double>& smoothed,
                             std::size_t smoother_span,
                             std::vector<double>& work_weights,
                             std::vector<double>& work_resid_weights) const;

    std::size_t seasonal_period_;
    std::size_t seasonal_smoother_;
    std::size_t trend_smoother_;
    std::size_t iterations_;
    bool robust_;

    std::vector<double> trend_;
    std::vector<double> seasonal_;
    std::vector<double> remainder_;
};

} // namespace anofoxtime::seasonality
