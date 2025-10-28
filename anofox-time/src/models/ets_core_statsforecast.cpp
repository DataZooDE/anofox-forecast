/**
 * ETS Core - Exact port of statsforecast's C++ implementation
 * Based on ets_cpp.txt from statsforecast
 */

#include "anofox-time/models/ets.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace anofoxtime::models {

constexpr double ETS_TOL = 1e-10;
constexpr double ETS_HUGE_N = 1e10;

/**
 * statsforecast Update function (ets_cpp.txt lines 25-89)
 * 
 * Updates ETS states given new observation
 * 
 * @param s Output: new seasonal array (size m), s[0] = newest
 * @param l Output: new level
 * @param b Output: new trend
 * @param old_l Input: previous level
 * @param old_b Input: previous trend
 * @param old_s Input: previous seasonal array (size m), old_s[m-1] = oldest
 * @param m Seasonal period
 * @param has_trend Whether model has trend component
 * @param trend_additive Whether trend is additive (vs multiplicative)
 * @param has_season Whether model has seasonal component
 * @param season_additive Whether season is additive (vs multiplicative)
 * @param alpha Level smoothing parameter
 * @param beta Trend smoothing parameter
 * @param gamma Seasonal smoothing parameter
 * @param phi Damping parameter
 * @param y Observation value
 * @return std::pair<double, double> {new_level, new_trend}
 */
std::pair<double, double> ets_update_statsforecast(
    std::vector<double>& s,
    double old_l,
    double old_b,
    const std::vector<double>& old_s,
    int m,
    bool has_trend,
    bool trend_additive,
    bool has_season,
    bool season_additive,
    double alpha,
    double beta,
    double gamma,
    double phi,
    double y)
{
    // Debug disabled for performance
    
    // Step 1: Compute one-step forecast q (ets_cpp.txt lines 32-43)
    double q, phi_b;
    if (!has_trend) {
        q = old_l;
        phi_b = 0.0;
    } else if (trend_additive) {
        phi_b = phi * old_b;
        q = old_l + phi_b;
    } else {
        // Multiplicative trend
        if (std::abs(phi - 1.0) < ETS_TOL) {
            phi_b = old_b;
            q = old_l * old_b;
        } else {
            phi_b = std::pow(old_b, phi);
            q = old_l * phi_b;
        }
    }
    
    // Step 2: Compute de-seasonalized observation p (ets_cpp.txt lines 46-57)
    double p;
    if (!has_season) {
        p = y;
    } else if (season_additive) {
        p = y - old_s[m - 1];
    } else {
        // Multiplicative seasonal
        if (std::abs(old_s[m - 1]) < ETS_TOL) {
            p = ETS_HUGE_N;
        } else {
            p = y / old_s[m - 1];
        }
    }
    
    // Step 3: New level (ets_cpp.txt line 58)
    double new_l = q + alpha * (p - q);
    
    // Step 4: New trend (ets_cpp.txt lines 60-71)
    double new_b = old_b;
    if (has_trend) {
        double r;
        if (trend_additive) {
            r = new_l - old_l;
        } else {
            // Multiplicative trend
            if (std::abs(old_l) < ETS_TOL) {
                r = ETS_HUGE_N;
            } else {
                r = new_l / old_l;
            }
        }
        new_b = phi_b + (beta / alpha) * (r - phi_b);
    }
    
    // Step 5: New seasonal (ets_cpp.txt lines 74-86)
    if (has_season) {
        double t;
        if (season_additive) {
            t = y - q;
        } else {
            // Multiplicative seasonal
            if (std::abs(q) < ETS_TOL) {
                t = ETS_HUGE_N;
            } else {
                t = y / q;
            }
        }
        
        // Update seasonal: s[0] = old_s[m-1] + gamma * (t - old_s[m-1])
        s[0] = old_s[m - 1] + gamma * (t - old_s[m - 1]);
        
        // Rotate: s[1..m-1] = old_s[0..m-2]
        std::copy(old_s.begin(), old_s.begin() + m - 1, s.begin() + 1);
    }
    
    return {new_l, new_b};
}

/**
 * statsforecast Forecast function (ets_cpp.txt lines 92-122)
 * 
 * Generates h-step ahead forecasts
 * 
 * @param forecast Output: forecast values (size h)
 * @param l Level
 * @param b Trend  
 * @param s Seasonal array (size m), s[0] = newest, s[m-1] = oldest
 * @param m Seasonal period
 * @param has_trend Whether model has trend
 * @param trend_additive Whether trend is additive
 * @param has_season Whether model has seasonal
 * @param season_additive Whether seasonal is additive
 * @param phi Damping parameter
 * @param h Forecast horizon
 */
void ets_forecast_statsforecast(
    std::vector<double>& forecast,
    double l,
    double b,
    const std::vector<double>& s,
    int m,
    bool has_trend,
    bool trend_additive,
    bool has_season,
    bool season_additive,
    double phi,
    int h)
{
    forecast.resize(h);
    
    double phistar = phi;
    for (int i = 0; i < h; ++i) {
        // Trend component
        if (!has_trend) {
            forecast[i] = l;
        } else if (trend_additive) {
            forecast[i] = l + phistar * b;
        } else {
            // Multiplicative trend
            if (b < 0) {
                forecast[i] = std::numeric_limits<double>::quiet_NaN();
            } else {
                forecast[i] = l * std::pow(b, phistar);
            }
        }
        
        // Seasonal component (ets_cpp.txt lines 105-113)
        // j = m - 1 - i, wrapping around
        int j = m - 1 - i;
        while (j < 0) {
            j += m;
        }
        
        if (season_additive) {
            forecast[i] += s[j];
        } else if (has_season) {
            forecast[i] *= s[j];
        }
        
        // Update phistar for next step (ets_cpp.txt lines 114-120)
        if (i < h - 1) {
            if (std::abs(phi - 1.0) < ETS_TOL) {
                phistar += 1.0;
            } else {
                phistar += std::pow(phi, i + 1);
            }
        }
    }
}

} // namespace anofoxtime::models

