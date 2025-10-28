#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/utils/logging.hpp"
#include <optional>
#include <vector>

namespace anofoxtime::models {

class GARCH {
public:
    GARCH(int p, int q, double omega, std::vector<double> alpha, std::vector<double> beta);

    void fit(const std::vector<double>& data);
    double forecastVariance(int horizon) const;

    const std::vector<double>& residuals() const { return residuals_; }
    const std::vector<double>& conditionalVariance() const { return sigma2_; }

private:
    void validateParameters() const;

    int p_;
    int q_;
    double omega_;
    std::vector<double> alpha_;
    std::vector<double> beta_;
    double mean_ = 0.0;
    std::vector<double> residuals_;
    std::vector<double> sigma2_;
};

} // namespace anofoxtime::models
