#include "anofox-time/seasonality/stl.hpp"
#include "../../third_party/CppLowess/Lowess.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace {

std::size_t ensure_odd(std::size_t window) {
    if (window < 3) return 3;
    return (window % 2 == 0) ? window + 1 : window;
}

double median(std::vector<double>& values) {  // Pass by reference to avoid copy
    if (values.empty()) return 0.0;
    std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
    double med = values[values.size() / 2];
    if (values.size() % 2 == 0) {
        auto max_it = std::max_element(values.begin(), values.begin() + values.size() / 2);
        med = (med + *max_it) / 2.0;
    }
    return med;
}

} // namespace

namespace anofoxtime::seasonality {

STLDecomposition::Builder& STLDecomposition::Builder::withPeriod(std::size_t period) {
    period_ = period;
    return *this;
}

STLDecomposition::Builder& STLDecomposition::Builder::withSeasonalSmoother(std::size_t window) {
    seasonal_smoother_ = window;
    return *this;
}

STLDecomposition::Builder& STLDecomposition::Builder::withTrendSmoother(std::size_t window) {
    trend_smoother_ = window;
    return *this;
}

STLDecomposition::Builder& STLDecomposition::Builder::withIterations(std::size_t iterations) {
    iterations_ = std::max<std::size_t>(1, iterations);
    return *this;
}

STLDecomposition::Builder& STLDecomposition::Builder::withRobust(bool robust) {
    robust_ = robust;
    return *this;
}

STLDecomposition STLDecomposition::Builder::build() const {
    return STLDecomposition(period_, seasonal_smoother_, trend_smoother_, iterations_, robust_);
}

STLDecomposition::Builder STLDecomposition::builder() {
    return Builder();
}

STLDecomposition::STLDecomposition(std::size_t seasonal_period,
                                   std::size_t seasonal_smoother,
                                   std::size_t trend_smoother,
                                   std::size_t iterations,
                                   bool robust)
    : seasonal_period_(seasonal_period),
      seasonal_smoother_(ensure_odd(seasonal_smoother)),
      trend_smoother_(ensure_odd(trend_smoother)),
      iterations_(std::max<std::size_t>(1, iterations)),
      robust_(robust) {
    if (seasonal_period_ < 2) {
        throw std::invalid_argument("Seasonal period must be at least 2.");
    }
}

void STLDecomposition::fit(const core::TimeSeries& ts) {
    const auto& data = ts.getValues();
    const std::size_t n = data.size();
    if (n < 2 * seasonal_period_) {
        throw std::invalid_argument("Insufficient data for STL decomposition.");
    }

    trend_.assign(n, 0.0);
    seasonal_.assign(n, 0.0);
    remainder_.assign(n, 0.0);

    std::vector<double> detrended(n, 0.0);
    std::vector<double> weights(n, 1.0);
    
    // Pre-allocate work vectors for LOESS to avoid repeated allocations
    std::vector<double> x_vec(n);
    for (std::size_t i = 0; i < n; ++i) {
        x_vec[i] = static_cast<double>(i);
    }
    std::vector<double> lowess_weights(n);
    std::vector<double> lowess_resid_weights(n, 1.0);
    
    // Pre-allocate seasonal work vectors outside iteration loop
    std::vector<double> seasonal_means(seasonal_period_, 0.0);
    std::vector<double> weight_totals(seasonal_period_, 0.0);
    std::vector<double> abs_residuals(n);  // Pre-allocate for robust weighting

    for (std::size_t iter = 0; iter < iterations_; ++iter) {
        // Use LOESS smoothing for trend instead of moving average
        applyLowessSmoothing(x_vec, data, trend_, trend_smoother_, lowess_weights, lowess_resid_weights);

        for (std::size_t i = 0; i < n; ++i) {
            detrended[i] = data[i] - trend_[i];
        }

        // Reset seasonal work vectors (reuse allocation)
        std::fill(seasonal_means.begin(), seasonal_means.end(), 0.0);
        std::fill(weight_totals.begin(), weight_totals.end(), 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            const std::size_t idx = i % seasonal_period_;
            seasonal_means[idx] += detrended[i] * weights[i];
            weight_totals[idx] += weights[i];
        }

        for (std::size_t j = 0; j < seasonal_period_; ++j) {
            if (weight_totals[j] > 0.0) {
                seasonal_means[j] /= weight_totals[j];
            }
        }

        const double seasonal_mean = std::accumulate(seasonal_means.begin(), seasonal_means.end(), 0.0) /
                                     static_cast<double>(seasonal_period_);
        for (double& value : seasonal_means) {
            value -= seasonal_mean;
        }

        for (std::size_t i = 0; i < n; ++i) {
            seasonal_[i] = seasonal_means[i % seasonal_period_];
            remainder_[i] = data[i] - trend_[i] - seasonal_[i];
        }

        if (robust_) {
            // Reuse pre-allocated abs_residuals vector
            for (std::size_t i = 0; i < n; ++i) {
                abs_residuals[i] = std::abs(remainder_[i]);
            }
            const double med = median(abs_residuals);
            if (med > 0.0) {
                for (std::size_t i = 0; i < n; ++i) {
                    double arg = remainder_[i] / (6.0 * med);
                    if (std::abs(arg) < 1.0) {
                        double w = 1.0 - arg * arg;
                        weights[i] = w * w;
                    } else {
                        weights[i] = 0.0;
                    }
                }
            }
        }
    }

    ANOFOX_INFO("STL decomposition performed with seasonal period {} using {} iterations.", seasonal_period_, iterations_);
}

double STLDecomposition::variance(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    const double mean = std::accumulate(values.begin(), values.end(), 0.0) /
                        static_cast<double>(values.size());
    double accum = 0.0;
    for (double v : values) {
        const double diff = v - mean;
        accum += diff * diff;
    }
    return accum / static_cast<double>(values.size());
}

void STLDecomposition::applyLowessSmoothing(const std::vector<double>& x,
                                            const std::vector<double>& y,
                                            std::vector<double>& smoothed,
                                            std::size_t smoother_span,
                                            std::vector<double>& work_weights,
                                            std::vector<double>& work_resid_weights) const {
    const std::size_t n = x.size();
    
    // Calculate fraction of data to use for LOESS
    // Convert window size to fraction
    double frac = static_cast<double>(smoother_span) / static_cast<double>(n);
    frac = std::max(0.01, std::min(1.0, frac)); // Clamp between 0.01 and 1.0
    
    // Use CppLowess for proper LOESS smoothing
    CppLowess::Lowess lowess;
    
    // Apply LOESS: nsteps=0 for non-robust, 2 for robust
    int nsteps = robust_ ? 2 : 0;
    double delta = 0.01 * (x[n-1] - x[0]); // Standard delta for efficiency
    
    lowess.lowess(x, y, frac, nsteps, delta, smoothed, work_resid_weights, work_weights);
}

double STLDecomposition::seasonalStrength() const {
    if (seasonal_.empty() || remainder_.empty()) {
        throw std::runtime_error("STL decomposition not fitted.");
    }
    std::vector<double> seasonal_plus_remainder(seasonal_.size());
    for (std::size_t i = 0; i < seasonal_.size(); ++i) {
        seasonal_plus_remainder[i] = seasonal_[i] + remainder_[i];
    }
    const double var_remainder = variance(remainder_);
    const double var_total = variance(seasonal_plus_remainder);
    if (var_total <= 0.0) {
        return 0.0;
    }
    return 1.0 - (var_remainder / var_total);
}

double STLDecomposition::trendStrength() const {
    if (trend_.empty() || remainder_.empty()) {
        throw std::runtime_error("STL decomposition not fitted.");
    }
    std::vector<double> trend_plus_remainder(trend_.size());
    for (std::size_t i = 0; i < trend_.size(); ++i) {
        trend_plus_remainder[i] = trend_[i] + remainder_[i];
    }
    const double var_remainder = variance(remainder_);
    const double var_total = variance(trend_plus_remainder);
    if (var_total <= 0.0) {
        return 0.0;
    }
    return 1.0 - (var_remainder / var_total);
}

} // namespace anofoxtime::seasonality
