#pragma once

#include "anofox-time/models/croston_classic.hpp"

namespace anofoxtime::models {

/**
 * @brief Syntetos-Boylan Approximation (SBA) for Croston's method
 * 
 * A debiased version of Croston's Classic method that applies a 0.95 
 * correction factor to reduce bias in the forecast.
 * 
 * Formula: ŷ_t = 0.95 * (ẑ_t / p̂_t)
 * 
 * Reference: Syntetos, A. A., & Boylan, J. E. (2005). The accuracy of 
 * intermittent demand estimates. International Journal of Forecasting, 21(2), 303-314.
 */
class CrostonSBA : public CrostonClassic {
public:
    CrostonSBA();
    
    std::string getName() const override {
        return "CrostonSBA";
    }
    
protected:
    static constexpr double DEBIASING_FACTOR = 0.95;
    
    double applyBiasFactor(double forecast) const override {
        return forecast * DEBIASING_FACTOR;
    }
};

} // namespace anofoxtime::models


