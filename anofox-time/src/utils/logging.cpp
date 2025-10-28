#include "anofox-time/utils/logging.hpp"

#ifndef ANOFOX_NO_LOGGING
#include <spdlog/sinks/stdout_color_sinks.h>

namespace anofoxtime::utils {

std::shared_ptr<spdlog::logger> Logging::logger_;

void Logging::init(spdlog::level::level_enum level) {
	if (!logger_) {
		logger_ = spdlog::stdout_color_mt("anofox-time");
	}
	logger_->set_level(level);
	logger_->flush_on(level);
}

std::shared_ptr<spdlog::logger> &Logging::getLogger() {
	if (!logger_) {
		// Initialize with default level if not already done.
		init();
	}
	return logger_;
}

} // namespace anofoxtime::utils

#else
// Stub implementation when logging is disabled

namespace anofoxtime::utils {

// Empty implementation - nothing to do

} // namespace anofoxtime::utils

#endif // ANOFOX_NO_LOGGING