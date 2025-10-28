#include "anofox-time/models/garch.hpp"
#include <numeric>
#include <stdexcept>

namespace anofoxtime::models {

GARCH::GARCH(int p, int q, double omega, std::vector<double> alpha, std::vector<double> beta)
    : p_(p), q_(q), omega_(omega), alpha_(std::move(alpha)), beta_(std::move(beta)) {
    if (p_ <= 0 || q_ <= 0) {
        throw std::invalid_argument("GARCH requires positive p and q orders.");
    }
    if (static_cast<int>(alpha_.size()) != p_ || static_cast<int>(beta_.size()) != q_) {
        throw std::invalid_argument("Alpha/Beta size must match p/q respectively.");
    }
    validateParameters();
}

void GARCH::validateParameters() const {
    if (omega_ <= 0.0) {
        throw std::invalid_argument("Omega must be positive.");
    }

    for (double a : alpha_) {
        if (a < 0.0) {
            throw std::invalid_argument("Alpha coefficients must be non-negative.");
        }
    }

    for (double b : beta_) {
        if (b < 0.0) {
            throw std::invalid_argument("Beta coefficients must be non-negative.");
        }
    }

    double sum = std::accumulate(alpha_.begin(), alpha_.end(), 0.0) +
                 std::accumulate(beta_.begin(), beta_.end(), 0.0);
    if (sum >= 1.0) {
        throw std::invalid_argument("Sum of alpha and beta must be < 1 for stationarity.");
    }
}

void GARCH::fit(const std::vector<double>& data) {
    if (data.size() < static_cast<std::size_t>(std::max(p_, q_))) {
        throw std::invalid_argument("Insufficient data for GARCH fitting.");
    }

    mean_ = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    residuals_.resize(data.size());
    sigma2_.resize(data.size());

    double init = 0.0;
    for (double x : data) {
        init += (x - mean_) * (x - mean_);
    }
    init /= data.size();

    std::fill(sigma2_.begin(), sigma2_.begin() + std::max(p_, q_), init);

    for (std::size_t t = 0; t < data.size(); ++t) {
        residuals_[t] = data[t] - mean_;
        if (t < static_cast<std::size_t>(std::max(p_, q_))) {
            continue;
        }

        double var = omega_;
        for (int i = 0; i < p_; ++i) {
            var += alpha_[i] * residuals_[t - i - 1] * residuals_[t - i - 1];
        }
        for (int j = 0; j < q_; ++j) {
            var += beta_[j] * sigma2_[t - j - 1];
        }

        sigma2_[t] = var;
    }

    ANOFOX_INFO("GARCH({},{}) model fitted. Omega={}, mean={}", p_, q_, omega_, mean_);
}

double GARCH::forecastVariance(int horizon) const {
    if (sigma2_.empty()) {
        throw std::runtime_error("GARCH model must be fitted before forecasting.");
    }
    if (horizon <= 0) {
        throw std::invalid_argument("Horizon must be positive.");
    }

    double last_var = sigma2_.back();
    double arch_sum = std::accumulate(alpha_.begin(), alpha_.end(), 0.0);
    double garch_sum = std::accumulate(beta_.begin(), beta_.end(), 0.0);

    double unconditional = omega_ / (1.0 - arch_sum - garch_sum);

    double variance = last_var;
    for (int h = 0; h < horizon; ++h) {
        variance = omega_ + (arch_sum + garch_sum) * variance;
    }

    return variance + unconditional;
}

} // namespace anofoxtime::models
