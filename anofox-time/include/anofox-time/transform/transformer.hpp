#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace anofoxtime::core {
struct Forecast;
}

namespace anofoxtime::transform {

class Transformer {
public:
	virtual ~Transformer() = default;

	virtual void fit(const std::vector<double> &data) = 0;
	virtual void transform(std::vector<double> &data) const = 0;
	virtual void inverseTransform(std::vector<double> &data) const = 0;

	virtual void fitTransform(std::vector<double> &data) {
		fit(data);
		transform(data);
	}
};

class Pipeline {
public:
	Pipeline() = default;
	explicit Pipeline(std::vector<std::unique_ptr<Transformer>> transformers);

	Pipeline(const Pipeline &) = delete;
	Pipeline &operator=(const Pipeline &) = delete;
	Pipeline(Pipeline &&) noexcept = default;
	Pipeline &operator=(Pipeline &&) noexcept = default;

	void addTransformer(std::unique_ptr<Transformer> transformer);

	[[nodiscard]] bool isFitted() const noexcept { return is_fitted_; }
	[[nodiscard]] std::size_t size() const noexcept { return transformers_.size(); }

	void fit(const std::vector<double> &data);
	void fitTransform(std::vector<double> &data);
	void transform(std::vector<double> &data) const;
	void inverseTransform(std::vector<double> &data) const;
	void inverseTransformForecast(core::Forecast &forecast) const;

private:
	void ensureFitted() const;
	void fitTransformInner(std::vector<double> &data);

	std::vector<std::unique_ptr<Transformer>> transformers_;
	bool is_fitted_ = false;
};

} // namespace anofoxtime::transform
