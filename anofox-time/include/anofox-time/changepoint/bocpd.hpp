#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace anofoxtime::changepoint {

struct NormalGammaPrior {
    double mu0;
    double kappa0;
    double alpha0;
    double beta0;
};

enum class HazardModel {
    Constant,
    Logistic
};

struct LogisticHazardParams {
    double h = -5.0;
    double a = 1.0;
    double b = 1.0;
};

class BocpdDetector {
public:
    class Builder {
    public:
        Builder &hazardLambda(double value) {
            hazard_lambda_ = value;
            hazard_model_ = HazardModel::Constant;
            return *this;
        }

        Builder &logisticHazard(double h, double a, double b) {
            hazard_model_ = HazardModel::Logistic;
            logistic_params_ = {h, a, b};
            return *this;
        }

        Builder &normalGammaPrior(NormalGammaPrior prior) {
            prior_ = prior;
            return *this;
        }

        Builder &maxRunLength(std::size_t value) {
            max_run_length_ = value;
            return *this;
        }

        Builder &enableTracing(bool value) {
            trace_enabled_ = value;
            return *this;
        }

        BocpdDetector build() const;

    private:
        double hazard_lambda_ = 250.0;
        NormalGammaPrior prior_{0.0, 1.0, 1.0, 1.0};
        std::size_t max_run_length_ = 1024;
        bool trace_enabled_ = false;
        HazardModel hazard_model_ = HazardModel::Constant;
        LogisticHazardParams logistic_params_{};
    };

    static Builder builder();

    std::vector<std::size_t> detect(const std::vector<double> &data) const;
    
    // Detect with probabilities
    struct DetectionResult {
        std::vector<std::size_t> changepoint_indices;
        std::vector<double> changepoint_probabilities;  // Probability at each time point
    };
    
    DetectionResult detectWithProbabilities(const std::vector<double> &data) const;

private:
    BocpdDetector(double hazard_lambda,
                  NormalGammaPrior prior,
                  std::size_t max_run_length,
                  bool trace_enabled,
                  HazardModel hazard_model,
                  LogisticHazardParams logistic_params);

    double hazard_lambda_;
    NormalGammaPrior prior_;
    std::size_t max_run_length_;
    bool trace_enabled_;
    HazardModel hazard_model_;
    LogisticHazardParams logistic_params_;
};

} // namespace anofoxtime::changepoint
