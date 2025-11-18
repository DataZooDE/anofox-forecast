#include "anofox-time/transform/transformer.hpp"
#include "anofox-time/core/forecast.hpp"
#include <stdexcept>

namespace anofoxtime::transform {

Pipeline::Pipeline(std::vector<std::unique_ptr<Transformer>> transformers)
    : transformers_(std::move(transformers)), is_fitted_(false) {
}

void Pipeline::addTransformer(std::unique_ptr<Transformer> transformer) {
    if (is_fitted_) {
        throw std::runtime_error("Cannot add transformers after pipeline is fitted");
    }
    transformers_.push_back(std::move(transformer));
}

void Pipeline::ensureFitted() const {
    if (!is_fitted_) {
        throw std::runtime_error("Pipeline must be fitted before transform operations");
    }
}

void Pipeline::fit(const std::vector<double> &data) {
    for (auto &transformer : transformers_) {
        transformer->fit(data);
    }
    is_fitted_ = true;
}

void Pipeline::fitTransformInner(std::vector<double> &data) {
    for (auto &transformer : transformers_) {
        transformer->fitTransform(data);
    }
}

void Pipeline::fitTransform(std::vector<double> &data) {
    fitTransformInner(data);
    is_fitted_ = true;
}

void Pipeline::transform(std::vector<double> &data) const {
    ensureFitted();
    for (const auto &transformer : transformers_) {
        transformer->transform(data);
    }
}

void Pipeline::inverseTransform(std::vector<double> &data) const {
    ensureFitted();
    // Apply inverse transforms in reverse order
    for (auto it = transformers_.rbegin(); it != transformers_.rend(); ++it) {
        (*it)->inverseTransform(data);
    }
}

void Pipeline::inverseTransformForecast(core::Forecast &forecast) const {
    ensureFitted();
    if (forecast.primary().empty()) {
        return;
    }
    
    // Apply inverse transform to forecast values
    auto &values = forecast.primary();
    for (auto it = transformers_.rbegin(); it != transformers_.rend(); ++it) {
        (*it)->inverseTransform(values);
    }
}

} // namespace anofoxtime::transform

