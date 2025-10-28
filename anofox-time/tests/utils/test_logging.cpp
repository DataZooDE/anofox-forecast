#include <catch2/catch_test_macros.hpp>

#include "anofox-time/utils/logging.hpp"

#include <spdlog/spdlog.h>

using anofoxtime::utils::Logging;

TEST_CASE("Logging initializes singleton logger", "[utils][logging]") {
	auto &logger_ref = Logging::getLogger();
	REQUIRE(logger_ref);

	const auto first_level = logger_ref->level();

	Logging::init(spdlog::level::debug);
	auto &logger_after_init = Logging::getLogger();

	REQUIRE(logger_ref.get() == logger_after_init.get());
	REQUIRE(logger_after_init->level() == spdlog::level::debug);
	REQUIRE(logger_after_init->flush_level() == spdlog::level::debug);

	// Restore to original level for downstream tests
	logger_after_init->set_level(first_level);
	logger_after_init->flush_on(first_level);
}
