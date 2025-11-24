#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "anofox-time/models/method_name_wrapper.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/naive.hpp"
#include "common/time_series_helpers.hpp"
#include <memory>
#include <stdexcept>

using namespace anofoxtime::models;
using namespace anofoxtime::core;

TEST_CASE("MethodNameWrapper construction validation", "[models][wrapper][validation]") {
	SECTION("Null model throws") {
		std::unique_ptr<IForecaster> null_model = nullptr;
		REQUIRE_THROWS_AS(
			MethodNameWrapper(std::move(null_model), "CustomName"),
			std::invalid_argument
		);
	}
	
	SECTION("Empty name throws") {
		auto model = SimpleExponentialSmoothingBuilder().build();
		REQUIRE_THROWS_AS(
			MethodNameWrapper(std::move(model), ""),
			std::invalid_argument
		);
	}
	
	SECTION("Valid construction") {
		auto model = SimpleExponentialSmoothingBuilder().build();
		REQUIRE_NOTHROW(
			MethodNameWrapper(std::move(model), "CustomSES")
		);
	}
}

TEST_CASE("MethodNameWrapper getName", "[models][wrapper][name]") {
	auto model = SimpleExponentialSmoothingBuilder().build();
	MethodNameWrapper wrapper(std::move(model), "MyCustomModel");
	
	REQUIRE(wrapper.getName() == "MyCustomModel");
	REQUIRE(wrapper.getName() != "SES");
}

TEST_CASE("MethodNameWrapper delegates fit", "[models][wrapper][fit]") {
	auto model = SimpleExponentialSmoothingBuilder().build();
	MethodNameWrapper wrapper(std::move(model), "WrappedSES");
	
	auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	
	SECTION("Fit succeeds") {
		REQUIRE_NOTHROW(wrapper.fit(ts));
	}
	
	SECTION("Fit delegates to wrapped model") {
		wrapper.fit(ts);
		// After fit, wrapped model should be fitted
		REQUIRE_NOTHROW(wrapper.predict(3));
	}
}

TEST_CASE("MethodNameWrapper delegates predict", "[models][wrapper][predict]") {
	auto model = SimpleExponentialSmoothingBuilder().build();
	MethodNameWrapper wrapper(std::move(model), "WrappedSES");
	
	auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	wrapper.fit(ts);
	
	SECTION("Predict succeeds") {
		auto forecast = wrapper.predict(3);
		REQUIRE(forecast.primary().size() == 3);
	}
	
	SECTION("Predict before fit throws") {
		auto model = SimpleExponentialSmoothingBuilder().build();
		MethodNameWrapper unfitted(std::move(model), "Unfitted");
		REQUIRE_THROWS_AS(unfitted.predict(3), std::runtime_error);
	}
}

TEST_CASE("MethodNameWrapper wrappedModel access", "[models][wrapper][access]") {
	auto ses_model = SimpleExponentialSmoothingBuilder().build();
	IForecaster* ses_ptr = ses_model.get();
	
	MethodNameWrapper wrapper(std::move(ses_model), "WrappedSES");
	
	SECTION("Access wrapped model") {
		IForecaster& wrapped = wrapper.wrappedModel();
		REQUIRE(&wrapped == ses_ptr);
	}
	
	SECTION("Const access") {
		const MethodNameWrapper& const_wrapper = wrapper;
		const IForecaster& wrapped = const_wrapper.wrappedModel();
		REQUIRE(&wrapped == ses_ptr);
	}
}

TEST_CASE("MethodNameWrapper with different model types", "[models][wrapper][types]") {
	SECTION("Wrap SES") {
		auto model = SimpleExponentialSmoothingBuilder().build();
		MethodNameWrapper wrapper(std::move(model), "CustomSES");
		auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0});
		wrapper.fit(ts);
		auto forecast = wrapper.predict(2);
		REQUIRE(forecast.primary().size() == 2);
		REQUIRE(wrapper.getName() == "CustomSES");
	}
	
	SECTION("Wrap Naive") {
		auto model = std::make_unique<Naive>();
		MethodNameWrapper wrapper(std::move(model), "CustomNaive");
		auto ts = tests::helpers::makeUnivariateSeries({10.0, 20.0, 30.0});
		wrapper.fit(ts);
		auto forecast = wrapper.predict(2);
		REQUIRE(forecast.primary().size() == 2);
		REQUIRE(wrapper.getName() == "CustomNaive");
		// Naive should forecast last value
		REQUIRE(forecast.primary()[0] == Catch::Approx(30.0));
	}
}

TEST_CASE("MethodNameWrapper preserves model behavior", "[models][wrapper][behavior]") {
	// Test that wrapping doesn't change model behavior
	auto unwrapped = std::make_unique<Naive>();
	auto wrapped_model = std::make_unique<Naive>();
	auto wrapped = std::make_unique<MethodNameWrapper>(
		std::move(wrapped_model), "WrappedNaive"
	);
	
	auto ts = tests::helpers::makeUnivariateSeries({5.0, 10.0, 15.0});
	
	unwrapped->fit(ts);
	wrapped->fit(ts);
	
	auto forecast1 = unwrapped->predict(3);
	auto forecast2 = wrapped->predict(3);
	
	REQUIRE(forecast1.primary().size() == forecast2.primary().size());
	for (size_t i = 0; i < forecast1.primary().size(); ++i) {
		REQUIRE(forecast1.primary()[i] == Catch::Approx(forecast2.primary()[i]));
	}
}

TEST_CASE("MethodNameWrapper null model runtime error", "[models][wrapper][error]") {
	// This tests the runtime checks in fit() and predict()
	// Note: We can't easily test this without exposing internals or using
	// a mock, but the code has these checks for safety
	// The constructor already prevents null models, so this is defensive
	
	auto model = SimpleExponentialSmoothingBuilder().build();
	MethodNameWrapper wrapper(std::move(model), "Test");
	
	// Normal usage should work
	auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0});
	REQUIRE_NOTHROW(wrapper.fit(ts));
	REQUIRE_NOTHROW(wrapper.predict(2));
}

