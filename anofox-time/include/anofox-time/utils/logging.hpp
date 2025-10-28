#pragma once

#ifndef ANOFOX_NO_LOGGING
#include <spdlog/spdlog.h>
#include <memory>

namespace anofoxtime::utils {

/**
 * @class Logging
 * @brief Provides a singleton interface to the spdlog logging library.
 *
 * This class ensures that a single logger instance is used throughout the
 * application, which can be configured at startup.
 */
class Logging {
public:
	/**
	 * @brief Gets the singleton logger instance.
	 * @return A shared pointer to the spdlog logger.
	 */
	static std::shared_ptr<spdlog::logger> &getLogger();

	/**
	 * @brief Initializes the logger with a specific logging level.
	 * @param level The minimum level of messages to log.
	 */
	static void init(spdlog::level::level_enum level = spdlog::level::info);

private:
	// Private constructor to enforce singleton pattern
	Logging() = default;

	// The single logger instance
	static std::shared_ptr<spdlog::logger> logger_;
};

} // namespace anofoxtime::utils

// --- Logger Macros for convenient access ---
#define ANOFOX_TRACE(...)    anofoxtime::utils::Logging::getLogger()->trace(__VA_ARGS__)
#define ANOFOX_DEBUG(...)    anofoxtime::utils::Logging::getLogger()->debug(__VA_ARGS__)
#define ANOFOX_INFO(...)     anofoxtime::utils::Logging::getLogger()->info(__VA_ARGS__)
#define ANOFOX_WARN(...)     anofoxtime::utils::Logging::getLogger()->warn(__VA_ARGS__)
#define ANOFOX_ERROR(...)    anofoxtime::utils::Logging::getLogger()->error(__VA_ARGS__)
#define ANOFOX_CRITICAL(...) anofoxtime::utils::Logging::getLogger()->critical(__VA_ARGS__)

#else
// No-op logging when spdlog is not available

namespace anofoxtime::utils {

class Logging {
public:
	static void init() {}
};

} // namespace anofoxtime::utils

// No-op macros
#define ANOFOX_TRACE(...)    do {} while(0)
#define ANOFOX_DEBUG(...)    do {} while(0)
#define ANOFOX_INFO(...)     do {} while(0)
#define ANOFOX_WARN(...)     do {} while(0)
#define ANOFOX_ERROR(...)    do {} while(0)
#define ANOFOX_CRITICAL(...) do {} while(0)

#endif // ANOFOX_NO_LOGGING