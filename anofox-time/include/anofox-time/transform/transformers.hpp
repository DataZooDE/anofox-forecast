#pragma once

#include "anofox-time/transform/transformer.hpp"
#include <optional>
#include <vector>

namespace anofoxtime::transform {

class LinearInterpolator final : public Transformer {
public:
	LinearInterpolator() = default;

	void fit(const std::vector<double> &data) override;
	void transform(std::vector<double> &data) const override;
	void inverseTransform(std::vector<double> &data) const override;
};

class Logit final : public Transformer {
public:
	Logit() = default;

	void fit(const std::vector<double> &data) override;
	void transform(std::vector<double> &data) const override;
	void inverseTransform(std::vector<double> &data) const override;
};

class Log final : public Transformer {
public:
	Log() = default;

	void fit(const std::vector<double> &data) override;
	void transform(std::vector<double> &data) const override;
	void inverseTransform(std::vector<double> &data) const override;
};

class MinMaxScaler final : public Transformer {
public:
	MinMaxScaler();

	MinMaxScaler &withScaledRange(double min, double max);
	MinMaxScaler &withDataRange(double min, double max);

	void fit(const std::vector<double> &data) override;
	void transform(std::vector<double> &data) const override;
	void inverseTransform(std::vector<double> &data) const override;

private:
	void ensureParams() const;
	void computeScale(double input_min, double input_max);

	double output_min_;
	double output_max_;
	bool has_params_;
	double input_min_;
	double input_max_;
	double scale_factor_;
	double offset_;
};

struct StandardScaleParams {
	double mean = 0.0;
	double std_dev = 1.0;

	static StandardScaleParams fromData(const std::vector<double> &data);
	static StandardScaleParams fromDataIgnoringNaNs(const std::vector<double> &data);
};

class StandardScaler final : public Transformer {
public:
	StandardScaler();

	StandardScaler &withParameters(StandardScaleParams params);
	StandardScaler &ignoreNaNs(bool ignore);

	void fit(const std::vector<double> &data) override;
	void transform(std::vector<double> &data) const override;
	void inverseTransform(std::vector<double> &data) const override;

private:
	void ensureParams() const;

	bool ignore_nans_;
	std::optional<StandardScaleParams> params_;
};

class BoxCox final : public Transformer {
public:
	BoxCox();

	BoxCox &withLambda(double lambda);
	BoxCox &ignoreNaNs(bool ignore);

	void fit(const std::vector<double> &data) override;
	void transform(std::vector<double> &data) const override;
	void inverseTransform(std::vector<double> &data) const override;

private:
	std::vector<double> prepareData(const std::vector<double> &data) const;
	void ensureLambda() const;

	double lambda_;
	bool has_lambda_;
	bool ignore_nans_;
};

class YeoJohnson final : public Transformer {
public:
	YeoJohnson();

	YeoJohnson &withLambda(double lambda);
	YeoJohnson &ignoreNaNs(bool ignore);

	void fit(const std::vector<double> &data) override;
	void transform(std::vector<double> &data) const override;
	void inverseTransform(std::vector<double> &data) const override;

private:
	std::vector<double> prepareData(const std::vector<double> &data) const;
	void ensureLambda() const;

	double lambda_;
	bool has_lambda_;
	bool ignore_nans_;
};

} // namespace anofoxtime::transform
