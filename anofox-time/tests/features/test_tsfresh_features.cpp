#include <catch2/catch.hpp>
#include "anofox-time/features/feature_types.hpp"
#include "anofox-time/features/feature_math.hpp"

using namespace anofoxtime::features;

static FeatureConfig BuildConfig(const std::string &name, std::vector<ParameterMap> params = {}) {
	FeatureConfig config;
	FeatureRequest request;
	request.name = name;
	if (params.empty()) {
		request.parameters.push_back(ParameterMap {});
	} else {
		request.parameters = std::move(params);
	}
	config.requests.push_back(request);
	return config;
}

TEST_CASE("tsfresh abs_energy matches sum of squares", "[tsfresh][features]") {
	Series series {1.0, 2.0, 3.0};
	auto config = BuildConfig("abs_energy");
	auto results = FeatureRegistry::Instance().Compute(series, config);
	REQUIRE(results.size() == 1);
	REQUIRE(results[0].name == "abs_energy");
	REQUIRE(results[0].value == Approx(14.0));
}

TEST_CASE("tsfresh autocorrelation lag handling", "[tsfresh][features]") {
	Series series {2.0, 4.0, 6.0, 8.0};
	ParameterMap params;
	params.entries["lag"] = static_cast<int64_t>(1);
	auto config = BuildConfig("autocorrelation", {params});
	auto results = FeatureRegistry::Instance().Compute(series, config);
	REQUIRE(results.size() == 1);
	REQUIRE(results[0].name.find("__lag_1") != std::string::npos);
	REQUIRE(results[0].value == Approx(1.0));
}

TEST_CASE("tsfresh quantile interpolation", "[tsfresh][features]") {
	Series series {2.0, 4.0, 8.0, 16.0};
	ParameterMap params;
	params.entries["q"] = 0.75;
	auto config = BuildConfig("quantile", {params});
	auto results = FeatureRegistry::Instance().Compute(series, config);
	REQUIRE(results.size() == 1);
	REQUIRE(results[0].value == Approx(12.0));
}

TEST_CASE("tsfresh linear_trend slope for increasing series", "[tsfresh][features]") {
	Series series {1.0, 3.0, 5.0, 7.0, 9.0};
	ParameterMap params;
	params.entries["attr"] = std::string("slope");
	auto config = BuildConfig("linear_trend", {params});
	auto results = FeatureRegistry::Instance().Compute(series, config);
	REQUIRE(results.size() == 1);
	REQUIRE(results[0].value == Approx(2.0));
}

TEST_CASE("tsfresh linear_trend_timewise honors timestamp spacing", "[tsfresh][features]") {
	Series series {1.0, 2.0, 3.5};
	std::vector<double> axis {0.0, 1.0, 3.0};
	ParameterMap params;
	params.entries["attr"] = std::string("slope");
	auto config = BuildConfig("linear_trend_timewise", {params});
	auto results = FeatureRegistry::Instance().Compute(series, config, &axis);
	REQUIRE(results.size() == 1);
	REQUIRE(results[0].value == Approx(1.0).margin(1e-6));
}

TEST_CASE("tsfresh fft_coefficient real part", "[tsfresh][features]") {
	Series series {0.0, 1.0, 0.0, -1.0};
	ParameterMap params;
	params.entries["coeff"] = static_cast<int64_t>(1);
	params.entries["attr"] = std::string("real");
	auto config = BuildConfig("fft_coefficient", {params});
	auto results = FeatureRegistry::Instance().Compute(series, config);
	REQUIRE(results.size() == 1);
	REQUIRE(results[0].value == Approx(0.0).margin(1e-6));
}

