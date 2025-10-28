#include "anofox-time/changepoint/bocpd.hpp"
#include "anofox-time/utils/logging.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace anofoxtime::changepoint {
namespace {
struct SufficientStats {
    double mu;
    double kappa;
    double alpha;
    double beta;
};

inline double log_student_t(double x, const SufficientStats &stats) {
    const double nu = 2.0 * stats.alpha;
    const double mu = stats.mu;
    const double scale_sq = stats.beta * (stats.kappa + 1.0) / (stats.alpha * stats.kappa);
    const double scale = std::sqrt(scale_sq);
    const double diff = (x - mu) / scale;
    const double term = 1.0 + (diff * diff) / nu;

    return std::lgamma((nu + 1.0) / 2.0) - std::lgamma(nu / 2.0)
         - 0.5 * std::log(nu * M_PI) - std::log(scale)
         - ((nu + 1.0) / 2.0) * std::log(term);
}

inline SufficientStats update_stats(const SufficientStats &stats, double x) {
    SufficientStats updated = stats;
    const double kappa_new = stats.kappa + 1.0;
    const double mu_new = (stats.kappa * stats.mu + x) / kappa_new;
    const double alpha_new = stats.alpha + 0.5;
    const double beta_new = stats.beta + 0.5 * stats.kappa * (x - stats.mu) * (x - stats.mu) / kappa_new;

    updated.mu = mu_new;
    updated.kappa = kappa_new;
    updated.alpha = alpha_new;
    updated.beta = beta_new;
    return updated;
}

inline double logsumexp(double a, double b) {
    if (a == -std::numeric_limits<double>::infinity()) return b;
    if (b == -std::numeric_limits<double>::infinity()) return a;
    if (a < b) std::swap(a, b);
    return a + std::log1p(std::exp(b - a));
}

inline double logsumexp(const std::vector<double> &values) {
    double max_val = -std::numeric_limits<double>::infinity();
    for (double v : values) {
        if (v > max_val) max_val = v;
    }
    if (!std::isfinite(max_val)) {
        return max_val;
    }
    double sum = 0.0;
    for (double v : values) {
        sum += std::exp(v - max_val);
    }
    return max_val + std::log(sum);
}
}

BocpdDetector::Builder BocpdDetector::builder() {
    return {};
}

BocpdDetector::BocpdDetector(double hazard_lambda,
                             NormalGammaPrior prior,
                             std::size_t max_run_length,
                             bool trace_enabled,
                             HazardModel hazard_model,
                             LogisticHazardParams logistic_params)
    : hazard_lambda_(hazard_lambda),
      prior_(prior),
      max_run_length_(std::max<std::size_t>(1, max_run_length)),
      trace_enabled_(trace_enabled),
      hazard_model_(hazard_model),
      logistic_params_(logistic_params) {}

BocpdDetector BocpdDetector::Builder::build() const {
    return BocpdDetector(hazard_lambda_, prior_, max_run_length_, trace_enabled_, hazard_model_, logistic_params_);
}

std::vector<std::size_t> BocpdDetector::detect(const std::vector<double> &data) const {
    std::vector<std::size_t> changepoints;
    const std::size_t n = data.size();
    if (n == 0) {
        return changepoints;
    }

    changepoints.push_back(0);
    if (n == 1) {
        return changepoints;
    }

    std::vector<double> log_run_probs(max_run_length_ + 1, -std::numeric_limits<double>::infinity());
    std::vector<SufficientStats> stats(max_run_length_ + 1);

    stats[0] = {prior_.mu0, prior_.kappa0, prior_.alpha0, prior_.beta0};
    log_run_probs[0] = 0.0;

#ifndef ANOFOX_NO_LOGGING
    auto logger = utils::Logging::getLogger();
#endif
    std::size_t prev_map_run = 0;

    for (std::size_t t = 0; t < n; ++t) {
        const double x = data[t];

        std::vector<double> log_pred(max_run_length_ + 1, -std::numeric_limits<double>::infinity());
        for (std::size_t r = 0; r <= max_run_length_; ++r) {
            if (!std::isfinite(log_run_probs[r])) continue;
            log_pred[r] = log_student_t(x, stats[r]);
        }

        std::vector<double> new_log_probs(max_run_length_ + 1, -std::numeric_limits<double>::infinity());
        std::vector<SufficientStats> new_stats(max_run_length_ + 1);

        double log_cp = -std::numeric_limits<double>::infinity();
        for (std::size_t r = 0; r <= max_run_length_; ++r) {
            if (!std::isfinite(log_run_probs[r])) continue;
            const double lp = log_run_probs[r] + log_pred[r];

            double hazard_prob;
            if (hazard_model_ == HazardModel::Constant) {
                hazard_prob = std::clamp(1.0 / hazard_lambda_, 1e-6, 0.999);
            } else {
                const double h = logistic_params_.h + logistic_params_.a * (static_cast<double>(r) - logistic_params_.b);
                hazard_prob = 1.0 / (1.0 + std::exp(-h));
                hazard_prob = std::clamp(hazard_prob, 1e-6, 0.999);
            }

            const double log_H = std::log(hazard_prob);
            const double log_1mH = std::log(1.0 - hazard_prob);

            log_cp = logsumexp(log_cp, lp + log_H);

            if (r + 1 <= max_run_length_) {
                const double growth = lp + log_1mH;
                new_log_probs[r + 1] = logsumexp(new_log_probs[r + 1], growth);
                new_stats[r + 1] = update_stats(stats[r], x);

#ifndef ANOFOX_NO_LOGGING
                if (trace_enabled_) {
                    logger->trace("BOCPD growth: t={} r={} -> r+1={} log_prob={}", t, r, r + 1, growth);
                }
#endif
            }
        }

        new_log_probs[0] = log_cp;
        new_stats[0] = update_stats({prior_.mu0, prior_.kappa0, prior_.alpha0, prior_.beta0}, x);

        const double log_norm = logsumexp(new_log_probs);
        for (double &v : new_log_probs) {
            v -= log_norm;
        }

        log_run_probs.swap(new_log_probs);
        stats.swap(new_stats);

        std::size_t map_run = 0;
        double best = -std::numeric_limits<double>::infinity();
        for (std::size_t r = 0; r <= max_run_length_; ++r) {
            if (log_run_probs[r] > best) {
                best = log_run_probs[r];
                map_run = r;
            }
        }

#ifndef ANOFOX_NO_LOGGING
        if (trace_enabled_) {
            logger->trace("BOCPD step={} map_run={} prob={}", t, map_run, std::exp(best));
        }
#endif

        if (map_run < prev_map_run && t > 0) {
            const std::size_t cp_index = (t > map_run) ? (t - map_run) : 0;
            if (changepoints.empty() || changepoints.back() != cp_index) {
                changepoints.push_back(cp_index);
            }
        }
        prev_map_run = map_run;
    }

    if (changepoints.back() != n - 1) {
        changepoints.push_back(n - 1);
    }

    std::sort(changepoints.begin(), changepoints.end());
    changepoints.erase(std::unique(changepoints.begin(), changepoints.end()), changepoints.end());
    return changepoints;
}

BocpdDetector::DetectionResult BocpdDetector::detectWithProbabilities(const std::vector<double> &data) const {
    DetectionResult result;
    const std::size_t n = data.size();
    
    result.changepoint_probabilities.resize(n, 0.0);
    
    if (n == 0) {
        return result;
    }

    result.changepoint_indices.push_back(0);
    result.changepoint_probabilities[0] = 1.0;  // First point is always a changepoint
    
    if (n == 1) {
        return result;
    }

    std::vector<double> log_run_probs(max_run_length_ + 1, -std::numeric_limits<double>::infinity());
    std::vector<SufficientStats> stats(max_run_length_ + 1);

    stats[0] = {prior_.mu0, prior_.kappa0, prior_.alpha0, prior_.beta0};
    log_run_probs[0] = 0.0;

    std::size_t prev_map_run = 0;

    for (std::size_t t = 0; t < n; ++t) {
        const double x = data[t];

        std::vector<double> log_pred(max_run_length_ + 1, -std::numeric_limits<double>::infinity());
        for (std::size_t r = 0; r <= max_run_length_; ++r) {
            if (!std::isfinite(log_run_probs[r])) continue;
            log_pred[r] = log_student_t(x, stats[r]);
        }

        std::vector<double> new_log_probs(max_run_length_ + 1, -std::numeric_limits<double>::infinity());
        std::vector<SufficientStats> new_stats(max_run_length_ + 1);

        double log_cp = -std::numeric_limits<double>::infinity();
        for (std::size_t r = 0; r <= max_run_length_; ++r) {
            if (!std::isfinite(log_run_probs[r])) continue;
            const double lp = log_run_probs[r] + log_pred[r];

            double hazard_prob;
            if (hazard_model_ == HazardModel::Constant) {
                hazard_prob = std::clamp(1.0 / hazard_lambda_, 1e-6, 0.999);
            } else {
                const double h = logistic_params_.h + logistic_params_.a * (static_cast<double>(r) - logistic_params_.b);
                hazard_prob = 1.0 / (1.0 + std::exp(-h));
                hazard_prob = std::clamp(hazard_prob, 1e-6, 0.999);
            }

            const double log_H = std::log(hazard_prob);
            const double log_1mH = std::log(1.0 - hazard_prob);

            log_cp = logsumexp(log_cp, lp + log_H);

            if (r + 1 <= max_run_length_) {
                const double growth = lp + log_1mH;
                new_log_probs[r + 1] = logsumexp(new_log_probs[r + 1], growth);
                new_stats[r + 1] = update_stats(stats[r], x);
            }
        }

        new_log_probs[0] = log_cp;
        new_stats[0] = update_stats({prior_.mu0, prior_.kappa0, prior_.alpha0, prior_.beta0}, x);

        const double log_norm = logsumexp(new_log_probs);
        for (double &v : new_log_probs) {
            v -= log_norm;
        }

        // Store changepoint probability (probability of run length 0)
        result.changepoint_probabilities[t] = std::exp(new_log_probs[0]);

        log_run_probs.swap(new_log_probs);
        stats.swap(new_stats);

        std::size_t map_run = 0;
        double best = -std::numeric_limits<double>::infinity();
        for (std::size_t r = 0; r <= max_run_length_; ++r) {
            if (log_run_probs[r] > best) {
                best = log_run_probs[r];
                map_run = r;
            }
        }

        if (map_run < prev_map_run && t > 0) {
            const std::size_t cp_index = (t > map_run) ? (t - map_run) : 0;
            if (result.changepoint_indices.empty() || result.changepoint_indices.back() != cp_index) {
                result.changepoint_indices.push_back(cp_index);
            }
        }
        prev_map_run = map_run;
    }

    if (result.changepoint_indices.back() != n - 1) {
        result.changepoint_indices.push_back(n - 1);
    }

    std::sort(result.changepoint_indices.begin(), result.changepoint_indices.end());
    result.changepoint_indices.erase(std::unique(result.changepoint_indices.begin(), result.changepoint_indices.end()), 
                                     result.changepoint_indices.end());
    return result;
}

} // namespace anofoxtime::changepoint
