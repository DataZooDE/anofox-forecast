#include "anofox-time/seasonality/mstl.hpp"
#include "anofox-time/utils/logging.hpp"
#include <algorithm>
#include <chrono>
#include <numeric>
#include <stdexcept>

namespace {

std::size_t ensure_odd(std::size_t window) {
    if (window < 3) return 3;
    return (window % 2 == 0) ? window + 1 : window;
}

void moving_average(const std::vector<double>& data, std::vector<double>& target, std::size_t window) {
    const std::size_t n = data.size();
    const std::size_t half = window / 2;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t start = (i > half) ? i - half : 0;
        const std::size_t end = std::min<std::size_t>(n - 1, i + half);
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t j = start; j <= end; ++j) {
            sum += data[j];
            ++count;
        }
        target[i] = (count > 0) ? sum / static_cast<double>(count) : data[i];
    }
}

} // namespace

namespace anofoxtime::seasonality {

MSTLDecomposition::Builder& MSTLDecomposition::Builder::withPeriods(std::vector<std::size_t> periods) {
    periods_ = std::move(periods);
    return *this;
}

MSTLDecomposition::Builder& MSTLDecomposition::Builder::withIterations(std::size_t iterations) {
    iterations_ = std::max<std::size_t>(1, iterations);
    return *this;
}

MSTLDecomposition::Builder& MSTLDecomposition::Builder::withRobust(bool robust) {
    robust_ = robust;
    return *this;
}

MSTLDecomposition MSTLDecomposition::Builder::build() const {
    return MSTLDecomposition(periods_, iterations_, robust_);
}

MSTLDecomposition::Builder MSTLDecomposition::builder() {
    return Builder();
}

MSTLDecomposition::MSTLDecomposition(std::vector<std::size_t> periods,
                                     std::size_t iterations,
                                     bool robust)
    : periods_(std::move(periods)),
      iterations_(std::max<std::size_t>(1, iterations)),
      robust_(robust) {
    periods_.erase(std::remove_if(periods_.begin(), periods_.end(), [](std::size_t p) { return p < 2; }), periods_.end());
    if (periods_.empty()) {
        throw std::invalid_argument("MSTL requires at least one seasonal period.");
    }
}

void MSTLDecomposition::fit(const core::TimeSeries& ts) {
    const auto& values = ts.getValues();
    const std::size_t n = values.size();
    if (n < 2 * (*std::min_element(periods_.begin(), periods_.end()))) {
        throw std::invalid_argument("Insufficient data for MSTL decomposition.");
    }

    // Initialize pre-allocated objects on first fit
    if (!is_initialized_ || work_timestamps_.size() != n) {
        // Pre-allocate STL decomposers (one per period)
        stl_decomposers_.clear();
        stl_decomposers_.reserve(periods_.size());
        for (std::size_t period : periods_) {
            stl_decomposers_.emplace_back(
                period,
                period,  // seasonal_smoother
                std::max<std::size_t>(ensure_odd(period * 3), 7),  // trend_smoother
                1,  // iterations
                robust_
            );
        }
        
        // Pre-allocate work vectors
        work_timestamps_.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            work_timestamps_[i] = core::TimeSeries::TimePoint{} + std::chrono::seconds(static_cast<long>(i));
        }
        work_residual_.resize(n);
        is_initialized_ = true;
    }

    components_.trend.assign(n, 0.0);
    components_.seasonal.assign(periods_.size(), std::vector<double>(n, 0.0));
    components_.remainder.assign(n, 0.0);

    // Use pre-allocated work vector instead of creating new one
    work_residual_.assign(values.begin(), values.end());

    for (std::size_t iter = 0; iter < iterations_; ++iter) {
        work_residual_.assign(values.begin(), values.end());

        for (std::size_t idx = 0; idx < periods_.size(); ++idx) {
            // Reuse pre-allocated STL decomposer
            core::TimeSeries temp_series(work_timestamps_, work_residual_);
            stl_decomposers_[idx].fit(temp_series);

            components_.seasonal[idx] = stl_decomposers_[idx].seasonal();
            for (std::size_t i = 0; i < n; ++i) {
                work_residual_[i] -= components_.seasonal[idx][i];
            }
        }

        // Estimate trend from work_residual after removing seasonalities.
        std::size_t trend_window = ensure_odd((*std::max_element(periods_.begin(), periods_.end())) * 2);
        moving_average(work_residual_, components_.trend, std::min(trend_window, n % 2 == 0 ? n - 1 : n));
        for (std::size_t i = 0; i < n; ++i) {
            components_.remainder[i] = values[i] - components_.trend[i];
            for (const auto& seasonal : components_.seasonal) {
                components_.remainder[i] -= seasonal[i];
            }
        }

        if (!robust_) {
            // Skip robustness weighting
            continue;
        }

        // Update work_residual for next iteration with robust weighting (simple clipping)
        double mad = 0.0;
        {
            std::vector<double> abs_res(components_.remainder.begin(), components_.remainder.end());
            for (double& v : abs_res) v = std::abs(v);
            std::nth_element(abs_res.begin(), abs_res.begin() + abs_res.size() / 2, abs_res.end());
            mad = abs_res[abs_res.size() / 2];
        }
        if (mad > 0.0) {
            const double c = 6.0 * mad;
            for (std::size_t i = 0; i < n; ++i) {
                double r = components_.remainder[i];
                const double factor = std::abs(r) > c ? (c / std::abs(r)) : 1.0;
                work_residual_[i] = values[i] - components_.trend[i];
                for (const auto& seasonal : components_.seasonal) {
                    work_residual_[i] -= seasonal[i];
                }
                work_residual_[i] *= factor;
            }
        }
    }

    ANOFOX_INFO("MSTL decomposition completed with {} seasonalities and {} iterations.", periods_.size(), iterations_);
}

} // namespace anofoxtime::seasonality
