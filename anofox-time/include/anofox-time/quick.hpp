#pragma once

#include "anofox-time/clustering/dbscan.hpp"
#include "anofox-time/core/distance_matrix.hpp"
#include "anofox-time/core/forecast.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/detectors/mad.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/models/dtw.hpp"
#include "anofox-time/models/holt.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/sma.hpp"
#include "anofox-time/outlier/dbscan_outlier.hpp"
#include "anofox-time/changepoint/bocpd.hpp"
#include "anofox-time/transform/transformer.hpp"
#include "anofox-time/seasonality/analyzer.hpp"
#include "anofox-time/seasonality/detector.hpp"
#include "anofox-time/validation.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace anofoxtime::quick {

// --- Internal Helpers ---
namespace internal {
inline core::TimeSeries series_from_vector(const std::vector<double> &data) {
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(data.size());
	auto now = std::chrono::system_clock::now();
	for (std::size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(now + std::chrono::seconds(i));
	}
	return core::TimeSeries(std::move(timestamps), data);
}

inline models::DTWBuilder configure_dtw_builder(models::DtwMetric metric,
                                                const std::optional<std::size_t> &window,
                                                const std::optional<double> &max_distance,
                                                const std::optional<double> &lower_bound,
                                                const std::optional<double> &upper_bound) {
	models::DTWBuilder builder;
	builder.withMetric(metric);
	if (window) {
		builder.withWindow(*window);
	}
	if (max_distance) {
		builder.withMaxDistance(*max_distance);
	}
	if (lower_bound) {
		builder.withLowerBound(*lower_bound);
	}
	if (upper_bound) {
		builder.withUpperBound(*upper_bound);
	}
	return builder;
}

inline std::string describe(models::ETSErrorType error) {
	switch (error) {
	case models::ETSErrorType::Additive:
		return "A";
	case models::ETSErrorType::Multiplicative:
		return "M";
	default:
		return "?";
	}
}

inline std::string describe(models::ETSTrendType trend) {
	switch (trend) {
	case models::ETSTrendType::None:
		return "N";
	case models::ETSTrendType::Additive:
		return "A";
	case models::ETSTrendType::DampedAdditive:
		return "Ad";
	default:
		return "?";
	}
}

inline std::string describe(models::ETSSeasonType season) {
	switch (season) {
	case models::ETSSeasonType::None:
		return "N";
	case models::ETSSeasonType::Additive:
		return "A";
	case models::ETSSeasonType::Multiplicative:
		return "M";
	default:
		return "?";
	}
}

struct CandidateDefinition {
	std::string name;
	std::function<std::unique_ptr<models::IForecaster>()> factory;
};

class PipelineForecaster final : public models::IForecaster {
public:
	PipelineForecaster(std::unique_ptr<models::IForecaster> inner,
	                   std::function<std::unique_ptr<transform::Pipeline>()> pipeline_factory)
	    : inner_(std::move(inner)), pipeline_factory_(std::move(pipeline_factory)) {
		if (!inner_) {
			throw std::invalid_argument("Pipeline forecaster requires a valid inner model.");
		}
	}

	void fit(const core::TimeSeries &ts) override {
		if (!pipeline_factory_) {
			inner_->fit(ts);
			return;
		}

		pipeline_ = pipeline_factory_();
		if (!pipeline_) {
			throw std::runtime_error("Pipeline factory returned null pipeline instance.");
		}
		auto transformed = transformSeries(ts, *pipeline_);
		inner_->fit(transformed);
	}

	core::Forecast predict(int horizon) override {
		core::Forecast forecast = inner_->predict(horizon);
		if (pipeline_) {
			pipeline_->inverseTransformForecast(forecast);
		}
		return forecast;
	}

	std::string getName() const override {
		return inner_->getName();
	}

private:
	static core::TimeSeries transformSeries(const core::TimeSeries &ts, transform::Pipeline &pipeline) {
		if (ts.dimensions() != 1) {
			throw std::invalid_argument("Pipeline preprocessing currently supports univariate series.");
		}

		std::vector<double> values(ts.getValues());
		pipeline.fitTransform(values);

		std::vector<std::vector<double>> columns;
		columns.reserve(ts.dimensions());
		columns.push_back(values);

		std::vector<core::TimeSeries::TimePoint> timestamps(ts.getTimestamps().begin(), ts.getTimestamps().end());
		auto labels = ts.labels();
		auto attributes = ts.attributes();

		core::TimeSeries transformed(std::move(timestamps), std::move(columns), core::TimeSeries::ValueLayout::ByColumn,
		                             std::move(labels), std::move(attributes));
		if (auto freq = ts.frequency()) {
			transformed.setFrequency(*freq);
		}
		return transformed;
	}

	std::unique_ptr<models::IForecaster> inner_;
	std::function<std::unique_ptr<transform::Pipeline>()> pipeline_factory_;
	std::unique_ptr<transform::Pipeline> pipeline_;
};

inline std::function<std::unique_ptr<models::IForecaster>()>
wrapFactoryWithPipeline(const std::function<std::unique_ptr<models::IForecaster>()> &factory,
                        const std::function<std::unique_ptr<transform::Pipeline>()> &pipeline_factory) {
	if (!pipeline_factory) {
		return factory;
	}

	return [factory, pipeline_factory]() -> std::unique_ptr<models::IForecaster> {
		if (!factory) {
			return nullptr;
		}
		auto inner = factory();
		if (!inner) {
			return nullptr;
		}
		return std::unique_ptr<models::IForecaster>(new PipelineForecaster(std::move(inner), pipeline_factory));
	};
}

inline std::optional<utils::AccuracyMetrics> computeMetrics(const core::Forecast &forecast,
                                                            const std::optional<std::vector<double>> &actual_primary,
                                                            const std::optional<std::vector<double>> &baseline_primary,
                                                            const std::optional<std::vector<std::vector<double>>> &actual_dimensions,
                                                            const std::optional<std::vector<std::vector<double>>> &baseline_dimensions) {
	const auto horizon = forecast.horizon();

	if (actual_dimensions) {
		if (actual_dimensions->size() != forecast.dimensions()) {
			throw std::invalid_argument("Actual dimension count must match forecast dimensions.");
		}
		for (const auto &dimension : *actual_dimensions) {
			if (dimension.size() != horizon) {
				throw std::invalid_argument("Actual dimension horizon must match forecast horizon.");
			}
		}
		if (baseline_dimensions) {
			if (baseline_dimensions->size() != actual_dimensions->size()) {
				throw std::invalid_argument("Baseline dimension count must match actual dimensions.");
			}
			for (const auto &dimension : *baseline_dimensions) {
				if (dimension.size() != horizon) {
					throw std::invalid_argument("Baseline dimension horizon must match forecast horizon.");
				}
			}
		}
		return validation::accuracyMetrics(*actual_dimensions, forecast.point, baseline_dimensions);
	}

	if (actual_primary) {
		if (actual_primary->size() != horizon) {
			throw std::invalid_argument("Actual vector must match forecast horizon.");
		}
		if (baseline_primary && baseline_primary->size() != actual_primary->size()) {
			throw std::invalid_argument("Baseline vector must match actual size for metrics.");
		}
		return validation::accuracyMetrics(*actual_primary, forecast.series(), baseline_primary);
	}

	return std::nullopt;
}
} // namespace internal

struct DtwOptions {
	models::DtwMetric metric = models::DtwMetric::Euclidean;
	std::optional<std::size_t> window;
	std::optional<double> max_distance;
	std::optional<double> lower_bound;
	std::optional<double> upper_bound;
};

struct ForecastSummary {
	core::Forecast forecast;
	std::optional<utils::AccuracyMetrics> metrics;
	std::optional<double> aic;
	std::optional<double> bic;
};

struct ETSOptions {
	models::ETSErrorType error = models::ETSErrorType::Additive;
	models::ETSTrendType trend = models::ETSTrendType::None;
	models::ETSSeasonType season = models::ETSSeasonType::None;
	int season_length = 0;
	double alpha = 0.2;
	std::optional<double> beta;
	std::optional<double> gamma;
	double phi = 0.98;
};

struct AutoSelectOptions {
	struct HoltConfig {
		double alpha = 0.3;
		double beta = 0.1;
	};

	struct ArimaConfig {
		int p = 1;
		int d = 1;
		int q = 0;
		bool include_intercept = true;
	};

	int horizon = 1;
	std::vector<int> sma_windows{3, 5};
	std::vector<double> ses_alphas{0.3};
	std::vector<HoltConfig> holt_params{{}};
	std::vector<ArimaConfig> arima_orders{{}};
	std::vector<ETSOptions> ets_configs;
	bool include_backtest = true;
	validation::RollingCVConfig backtest_config{};
	core::TimeSeries::SanitizeOptions sanitize;
	std::optional<core::TimeSeries::InterpolationOptions> interpolation;
	bool infer_frequency = false;
	std::chrono::nanoseconds frequency_tolerance{0};
	std::optional<std::vector<double>> actual;
	std::optional<std::vector<double>> baseline;
	std::function<std::optional<std::vector<double>>(const core::TimeSeries &, const core::TimeSeries &)> baseline_provider;
	std::function<std::unique_ptr<transform::Pipeline>()> pipeline_factory;
};

struct AutoSelectCandidateSummary {
	std::string name;
	ForecastSummary forecast;
	std::optional<validation::RollingBacktestSummary> backtest;
	double score = std::numeric_limits<double>::quiet_NaN();
};

struct AutoSelectResult {
	std::string model_name;
	ForecastSummary forecast;
	std::vector<AutoSelectCandidateSummary> candidates;
	std::vector<std::pair<std::string, std::string>> failures;
};

namespace internal {

inline core::TimeSeries preprocessSeries(const std::vector<double> &data, const AutoSelectOptions &options) {
	core::TimeSeries series = series_from_vector(data);
	if (series.hasMissingValues()) {
		series = series.sanitized(options.sanitize);
	}
	if (options.interpolation.has_value()) {
		series = series.interpolated(*options.interpolation);
	}
	if (options.infer_frequency) {
		series.setFrequencyFromTimestamps(options.frequency_tolerance);
	}
	return series;
}

inline std::size_t etsParameterCount(const models::ETSConfig &config) {
	std::size_t states = 1; // level
	if (config.trend != models::ETSTrendType::None) {
		states += 1; // trend
	}
	if (config.season != models::ETSSeasonType::None) {
		const int length = std::max(config.season_length, 0);
		states += static_cast<std::size_t>(length);
	}

	std::size_t smoothing = 1; // alpha
	if (config.trend != models::ETSTrendType::None) {
		smoothing += 1; // beta
		if (config.trend == models::ETSTrendType::DampedAdditive) {
			smoothing += 1; // phi
		}
	}
	if (config.season != models::ETSSeasonType::None) {
		smoothing += 1; // gamma
	}

	return states + smoothing;
}

inline std::vector<CandidateDefinition> makeCandidates(const AutoSelectOptions &options) {
	std::vector<CandidateDefinition> definitions;
	definitions.reserve(options.sma_windows.size() + options.ses_alphas.size() + options.holt_params.size() +
	                    options.arima_orders.size() + options.ets_configs.size());

	for (int window : options.sma_windows) {
		if (window <= 0) {
			continue;
		}
		std::ostringstream oss;
		oss << "SMA(window=" << window << ")";
		std::function<std::unique_ptr<models::IForecaster>()> base_factory =
		    [window]() { return models::SimpleMovingAverageBuilder().withWindow(window).build(); };
		definitions.push_back(
		    CandidateDefinition{oss.str(), wrapFactoryWithPipeline(base_factory, options.pipeline_factory)});
	}

	for (double alpha : options.ses_alphas) {
		if (!(alpha > 0.0 && alpha <= 1.0)) {
			continue;
		}
		std::ostringstream oss;
		oss << "SES(alpha=" << alpha << ")";
		std::function<std::unique_ptr<models::IForecaster>()> base_factory =
		    [alpha]() { return models::SimpleExponentialSmoothingBuilder().withAlpha(alpha).build(); };
		definitions.push_back(
		    CandidateDefinition{oss.str(), wrapFactoryWithPipeline(base_factory, options.pipeline_factory)});
	}

	for (const auto &params : options.holt_params) {
		if (!(params.alpha > 0.0 && params.alpha <= 1.0 && params.beta > 0.0 && params.beta <= 1.0)) {
			continue;
		}
		std::ostringstream oss;
		oss << "Holt(alpha=" << params.alpha << ",beta=" << params.beta << ")";
		std::function<std::unique_ptr<models::IForecaster>()> base_factory =
		    [params]() { return models::HoltLinearTrendBuilder().withAlpha(params.alpha).withBeta(params.beta).build(); };
		definitions.push_back(
		    CandidateDefinition{oss.str(), wrapFactoryWithPipeline(base_factory, options.pipeline_factory)});
	}

	for (const auto &order : options.arima_orders) {
		if (order.p < 0 || order.d < 0 || order.q < 0) {
			continue;
		}
		std::ostringstream oss;
		oss << "ARIMA(" << order.p << ',' << order.d << ',' << order.q << ')';
		std::function<std::unique_ptr<models::IForecaster>()> base_factory = [order]() {
			return models::ARIMABuilder()
			    .withAR(order.p)
			    .withDifferencing(order.d)
			    .withMA(order.q)
			    .withIntercept(order.include_intercept)
			    .build();
		};
		definitions.push_back(
		    CandidateDefinition{oss.str(), wrapFactoryWithPipeline(base_factory, options.pipeline_factory)});
	}

	for (const auto &opt : options.ets_configs) {
		models::ETSConfig cfg;
		cfg.error = opt.error;
		cfg.trend = opt.trend;
		cfg.season = opt.season;
		cfg.season_length = opt.season_length;
		cfg.alpha = opt.alpha;
		cfg.phi = opt.phi;
		if (cfg.trend != models::ETSTrendType::None) {
			cfg.beta = opt.beta.value_or(0.1);
		}
		if (cfg.season != models::ETSSeasonType::None) {
			cfg.gamma = opt.gamma.value_or(0.1);
		}

		std::ostringstream oss;
		oss << "ETS(error=" << describe(cfg.error) << ",trend=" << describe(cfg.trend)
		    << ",season=" << describe(cfg.season) << ')';

		std::function<std::unique_ptr<models::IForecaster>()> base_factory =
		    [cfg]() { return std::unique_ptr<models::IForecaster>(new models::ETS(cfg)); };
		definitions.push_back(
		    CandidateDefinition{oss.str(), wrapFactoryWithPipeline(base_factory, options.pipeline_factory)});
	}

	return definitions;
}

} // namespace internal

inline ForecastSummary movingAverage(const std::vector<double> &data, int window, int horizon,
                                     const std::optional<std::vector<double>> &actual = std::nullopt,
                                     const std::optional<std::vector<double>> &baseline = std::nullopt,
                                     const std::optional<std::vector<std::vector<double>>> &actual_dimensions = std::nullopt,
                                     const std::optional<std::vector<std::vector<double>>> &baseline_dimensions = std::nullopt) {
	auto ts = internal::series_from_vector(data);
	if (ts.isEmpty())
		return {};

	auto model = models::SimpleMovingAverageBuilder().withWindow(window).build();
	model->fit(ts);
	auto forecast = model->predict(horizon);

	ForecastSummary summary{std::move(forecast), std::nullopt};
	summary.metrics =
	    internal::computeMetrics(summary.forecast, actual, baseline, actual_dimensions, baseline_dimensions);
	return summary;
}

inline ForecastSummary arima(const std::vector<double> &data, int p, int d, int q, int horizon,
                             const std::optional<std::vector<double>> &actual = std::nullopt,
                             const std::optional<std::vector<double>> &baseline = std::nullopt,
                             bool include_intercept = true,
                             const std::optional<std::vector<std::vector<double>>> &actual_dimensions = std::nullopt,
                             const std::optional<std::vector<std::vector<double>>> &baseline_dimensions = std::nullopt) {
	auto ts = internal::series_from_vector(data);
	if (ts.isEmpty())
		return {};

	auto model = models::ARIMABuilder()
	                 .withAR(p)
	                 .withDifferencing(d)
	                 .withMA(q)
	                 .withIntercept(include_intercept)
	                 .build();
	model->fit(ts);
	auto forecast = model->predict(horizon);

	ForecastSummary summary{std::move(forecast), std::nullopt, model->aic(), model->bic()};
	summary.metrics =
	    internal::computeMetrics(summary.forecast, actual, baseline, actual_dimensions, baseline_dimensions);
	return summary;
}

inline ForecastSummary arimaWithConfidence(const std::vector<double> &data, int p, int d, int q,
                                           int horizon, double confidence,
                                           const std::optional<std::vector<double>> &actual = std::nullopt,
                                           const std::optional<std::vector<double>> &baseline = std::nullopt,
                                           bool include_intercept = true,
                                           const std::optional<std::vector<std::vector<double>>> &actual_dimensions = std::nullopt,
                                           const std::optional<std::vector<std::vector<double>>> &baseline_dimensions = std::nullopt) {
	auto ts = internal::series_from_vector(data);
	if (ts.isEmpty())
		return {};

	auto model = models::ARIMABuilder()
	                 .withAR(p)
	                 .withDifferencing(d)
	                 .withMA(q)
	                 .withIntercept(include_intercept)
	                 .build();

	model->fit(ts);
	auto forecast = model->predictWithConfidence(horizon, confidence);

	ForecastSummary summary{std::move(forecast), std::nullopt, model->aic(), model->bic()};
	summary.metrics =
	    internal::computeMetrics(summary.forecast, actual, baseline, actual_dimensions, baseline_dimensions);
	return summary;
}

inline validation::RollingBacktestSummary rollingBacktestSMA(
    const std::vector<double> &data,
    const validation::RollingCVConfig &config,
    int window,
    const std::function<std::optional<std::vector<double>>(const core::TimeSeries &, const core::TimeSeries &)> &baseline_provider = {},
    const std::function<std::unique_ptr<transform::Pipeline>()> &pipeline_factory = {}) {
	const auto series = internal::series_from_vector(data);
	if (series.isEmpty()) {
		throw std::invalid_argument("Rolling backtest requires non-empty data.");
	}

	std::function<std::unique_ptr<models::IForecaster>()> base_factory =
	    [window]() { return models::SimpleMovingAverageBuilder().withWindow(window).build(); };
	auto factory = internal::wrapFactoryWithPipeline(base_factory, pipeline_factory);

	return validation::rollingBacktest(series, config, factory, baseline_provider);
}

inline validation::RollingBacktestSummary rollingBacktestARIMA(
    const std::vector<double> &data,
    const validation::RollingCVConfig &config,
    int p, int d, int q,
    bool include_intercept = true,
    const std::function<std::optional<std::vector<double>>(const core::TimeSeries &, const core::TimeSeries &)> &baseline_provider = {},
    const std::function<std::unique_ptr<transform::Pipeline>()> &pipeline_factory = {}) {
	const auto series = internal::series_from_vector(data);
	if (series.isEmpty()) {
		throw std::invalid_argument("Rolling backtest requires non-empty data.");
	}

	std::function<std::unique_ptr<models::IForecaster>()> base_factory = [p, d, q, include_intercept]() {
		return models::ARIMABuilder()
		    .withAR(p)
		    .withDifferencing(d)
		    .withMA(q)
		    .withIntercept(include_intercept)
		    .build();
	};
	auto factory = internal::wrapFactoryWithPipeline(base_factory, pipeline_factory);

	return validation::rollingBacktest(series, config, factory, baseline_provider);
}

inline AutoSelectResult autoSelect(const std::vector<double> &data, const AutoSelectOptions &options = {}) {
	if (options.horizon <= 0) {
		throw std::invalid_argument("AutoSelect horizon must be a positive integer.");
	}
	if (data.empty()) {
		throw std::invalid_argument("AutoSelect requires non-empty data.");
	}
	if (options.actual && options.actual->size() != static_cast<std::size_t>(options.horizon)) {
		throw std::invalid_argument("Actual vector length must match forecast horizon for auto-select.");
	}
	if (options.baseline && options.baseline->size() != static_cast<std::size_t>(options.horizon)) {
		throw std::invalid_argument("Baseline vector length must match forecast horizon for auto-select.");
	}

	core::TimeSeries series = internal::preprocessSeries(data, options);
	if (series.size() < static_cast<std::size_t>(options.horizon)) {
		throw std::invalid_argument("Not enough observations to produce forecast with requested horizon.");
	}

	std::vector<internal::CandidateDefinition> definitions = internal::makeCandidates(options);
	if (definitions.empty()) {
		throw std::invalid_argument("AutoSelect received no viable candidate models.");
	}

	validation::RollingCVConfig base_cv = options.backtest_config;
	if (base_cv.horizon == 0) {
		base_cv.horizon = options.horizon;
	}
	if (base_cv.max_folds == 0) {
		base_cv.max_folds = 3;
	}
	if (base_cv.min_train < static_cast<std::size_t>(base_cv.horizon + 1)) {
		base_cv.min_train = base_cv.horizon + 1;
	}

	AutoSelectResult result;
	double best_score = std::numeric_limits<double>::infinity();
	bool have_score = false;

	for (const auto &definition : definitions) {
		AutoSelectCandidateSummary summary;
		summary.name = definition.name;

		try {
			validation::RollingCVConfig cv = base_cv;
			if (series.size() < cv.min_train + cv.horizon) {
				if (series.size() <= cv.horizon) {
					throw std::invalid_argument("Insufficient data for rolling backtest.");
				}
				cv.min_train = series.size() - cv.horizon;
			}

			auto model = definition.factory();
			model->fit(series);
			auto forecast = model->predict(options.horizon);
			summary.forecast.forecast = std::move(forecast);
			summary.forecast.metrics =
			    internal::computeMetrics(summary.forecast.forecast, options.actual, options.baseline, std::nullopt, std::nullopt);

			if (auto *arima_model = dynamic_cast<models::ARIMA *>(model.get())) {
				summary.forecast.aic = arima_model->aic();
				summary.forecast.bic = arima_model->bic();
			}

			if (auto *ets_model = dynamic_cast<models::ETS *>(model.get())) {
				const std::size_t parameter_count = internal::etsParameterCount(ets_model->config());
				summary.forecast.aic = ets_model->aic(parameter_count);
				const double log_likelihood = ets_model->logLikelihood();
				const std::size_t sample_size = ets_model->sampleSize();
				if (sample_size > 0 && std::isfinite(log_likelihood)) {
					const double bic =
					    -2.0 * log_likelihood + static_cast<double>(parameter_count) *
					                                std::log(static_cast<double>(sample_size));
					summary.forecast.bic = bic;
				} else {
					summary.forecast.bic.reset();
				}
			}

			if (options.include_backtest) {
				summary.backtest = validation::rollingBacktest(series, cv, definition.factory, options.baseline_provider);
				summary.score = summary.backtest->aggregate.mae;
				if (std::isfinite(summary.score)) {
					if (!have_score || summary.score < best_score) {
						best_score = summary.score;
						result.model_name = summary.name;
						result.forecast = summary.forecast;
						have_score = true;
					}
				}
			} else if (summary.forecast.metrics.has_value()) {
				summary.score = summary.forecast.metrics->mae;
				if (!have_score || summary.score < best_score) {
					best_score = summary.score;
					result.model_name = summary.name;
					result.forecast = summary.forecast;
					have_score = true;
				}
			}

			if (!have_score && result.candidates.empty()) {
				result.model_name = summary.name;
				result.forecast = summary.forecast;
			}

			result.candidates.push_back(std::move(summary));
		} catch (const std::exception &e) {
			result.failures.emplace_back(summary.name.empty() ? definition.name : summary.name, e.what());
		}
	}

	if (result.model_name.empty()) {
		if (!result.candidates.empty()) {
			result.model_name = result.candidates.front().name;
			result.forecast = result.candidates.front().forecast;
		} else {
			throw std::runtime_error("AutoSelect could not evaluate any candidates.");
		}
	}

	return result;
}

inline ForecastSummary simpleExponentialSmoothing(const std::vector<double> &data, double alpha, int horizon,
                                                  const std::optional<std::vector<double>> &actual = std::nullopt,
                                                  const std::optional<std::vector<double>> &baseline = std::nullopt,
                                                  const std::optional<std::vector<std::vector<double>>> &actual_dimensions = std::nullopt,
                                                  const std::optional<std::vector<std::vector<double>>> &baseline_dimensions = std::nullopt) {
	auto ts = internal::series_from_vector(data);
	if (ts.isEmpty())
		return {};

	auto model = models::SimpleExponentialSmoothingBuilder().withAlpha(alpha).build();
	model->fit(ts);
	auto forecast = model->predict(horizon);

	ForecastSummary summary{std::move(forecast), std::nullopt};
	summary.metrics =
	    internal::computeMetrics(summary.forecast, actual, baseline, actual_dimensions, baseline_dimensions);
	return summary;
}

inline ForecastSummary holtLinearTrend(const std::vector<double> &data, double alpha, double beta, int horizon,
                                       const std::optional<std::vector<double>> &actual = std::nullopt,
                                       const std::optional<std::vector<double>> &baseline = std::nullopt,
                                       const std::optional<std::vector<std::vector<double>>> &actual_dimensions = std::nullopt,
                                       const std::optional<std::vector<std::vector<double>>> &baseline_dimensions = std::nullopt) {
	auto ts = internal::series_from_vector(data);
	if (ts.isEmpty())
		return {};

	auto model = models::HoltLinearTrendBuilder().withAlpha(alpha).withBeta(beta).build();
	model->fit(ts);
	auto forecast = model->predict(horizon);

	ForecastSummary summary{std::move(forecast), std::nullopt};
	summary.metrics =
	    internal::computeMetrics(summary.forecast, actual, baseline, actual_dimensions, baseline_dimensions);
	return summary;
}

inline ForecastSummary ets(const std::vector<double> &data, int horizon,
                           const ETSOptions &options = {},
                           const std::optional<std::vector<double>> &actual = std::nullopt,
                           const std::optional<std::vector<double>> &baseline = std::nullopt,
                           const std::optional<std::vector<std::vector<double>>> &actual_dimensions = std::nullopt,
                           const std::optional<std::vector<std::vector<double>>> &baseline_dimensions = std::nullopt) {
	auto ts = internal::series_from_vector(data);
	if (ts.isEmpty())
		return {};

	models::ETSConfig config;
	config.error = options.error;
	config.trend = options.trend;
	config.season = options.season;
	config.season_length = options.season_length;
	config.alpha = options.alpha;
	config.phi = options.phi;

	if (config.trend != models::ETSTrendType::None) {
		config.beta = options.beta.value_or(0.1);
	}
	if (config.season != models::ETSSeasonType::None) {
		config.gamma = options.gamma.value_or(0.1);
		if (config.season_length < 2) {
			throw std::invalid_argument("ETS season length must be >= 2 when seasonality is enabled.");
		}
	}

	auto model = std::unique_ptr<models::ETS>(new models::ETS(config));
	model->fit(ts);
	auto forecast = model->predict(horizon);

	ForecastSummary summary{std::move(forecast), std::nullopt};
	summary.metrics =
	    internal::computeMetrics(summary.forecast, actual, baseline, actual_dimensions, baseline_dimensions);
	return summary;
}

inline double dtwDistance(const std::vector<double> &lhs, const std::vector<double> &rhs,
                          const DtwOptions &options = {}) {
	auto dtw =
	    internal::configure_dtw_builder(options.metric,
	                                    options.window,
	                                    options.max_distance,
	                                    options.lower_bound,
	                                    options.upper_bound)
	        .build();
	return dtw->distance(lhs, rhs);
}

inline core::DistanceMatrix dtwDistanceMatrix(const std::vector<std::vector<double>> &series,
                                              const DtwOptions &options = {}) {
	auto dtw =
	    internal::configure_dtw_builder(options.metric,
	                                    options.window,
	                                    options.max_distance,
	                                    options.lower_bound,
	                                    options.upper_bound)
	        .build();
	return dtw->distanceMatrix(series);
}

inline detectors::OutlierResult detectOutliersMAD(const std::vector<double> &data, double threshold = 3.5) {
	auto ts = internal::series_from_vector(data);
	if (ts.isEmpty())
		return {};

	auto detector = detectors::MADDetectorBuilder().withThreshold(threshold).build();
	return detector->detect(ts);
}

inline std::vector<int> clusterDbscan(const core::DistanceMatrix &matrix, double epsilon,
                                      std::size_t min_cluster_size) {
	auto clusterer = clustering::DbscanBuilder()
	                     .withEpsilon(epsilon)
	                     .withMinClusterSize(min_cluster_size)
	                     .build();
	return clusterer->clusterLabels(matrix);
}

inline std::vector<int> clusterDbscan(const std::vector<std::vector<double>> &series,
                                      double epsilon, std::size_t min_cluster_size,
                                      const DtwOptions &options = {}) {
	auto matrix = dtwDistanceMatrix(series, options);
	return clusterDbscan(matrix, epsilon, min_cluster_size);
}

inline outlier::OutlierDetectionResult detectOutliersDBSCAN(const core::DistanceMatrix &matrix,
                                                            double epsilon,
                                                            std::size_t min_cluster_size) {
	auto detector = outlier::DbscanOutlierBuilder()
	                    .withEpsilon(epsilon)
	                    .withMinClusterSize(min_cluster_size)
	                    .build();
	return detector->detect(matrix);
}

inline outlier::OutlierDetectionResult detectOutliersDBSCAN(
    const std::vector<std::vector<double>> &series, double epsilon, std::size_t min_cluster_size,
    const DtwOptions &options = {}) {
    auto matrix = dtwDistanceMatrix(series, options);
    return detectOutliersDBSCAN(matrix, epsilon, min_cluster_size);
}

inline std::vector<std::size_t> detectChangepoints(
    const std::vector<double> &data,
    double hazard_lambda = 250.0,
    const changepoint::NormalGammaPrior &prior = {0.0, 1.0, 1.0, 1.0},
    std::size_t max_run_length = 1024,
    std::optional<changepoint::LogisticHazardParams> logistic_params = std::nullopt) {
    auto builder = changepoint::BocpdDetector::builder()
                       .normalGammaPrior(prior)
                       .maxRunLength(max_run_length);
    if (logistic_params) {
        builder.logisticHazard(logistic_params->h, logistic_params->a, logistic_params->b);
    } else {
        builder.hazardLambda(hazard_lambda);
    }
    auto detector = builder.build();
    return detector.detect(data);
}

inline seasonality::SeasonalityAnalysis analyzeSeasonality(const core::TimeSeries &ts,
                                                           std::optional<std::size_t> override_period = std::nullopt) {
	seasonality::SeasonalityAnalyzer analyzer(seasonality::SeasonalityDetector::Builder().build());
	return analyzer.analyze(ts, override_period);
}

inline seasonality::SeasonalityAnalysis analyzeSeasonality(const core::TimeSeries &ts,
                                                           const seasonality::SeasonalityDetector &detector,
                                                           std::optional<std::size_t> override_period) {
	seasonality::SeasonalityAnalyzer analyzer(detector);
	return analyzer.analyze(ts, override_period);
}

} // namespace anofoxtime::quick
