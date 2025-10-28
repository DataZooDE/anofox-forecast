#pragma once

#include "anofox-time/core/time_series.hpp"

#include <chrono>
#include <vector>

namespace tests::helpers {

inline std::vector<anofoxtime::core::TimeSeries::TimePoint>
makeTimestamps(std::size_t count, std::chrono::seconds step = std::chrono::seconds{1}) {
	using TimePoint = anofoxtime::core::TimeSeries::TimePoint;
	std::vector<TimePoint> timestamps;
	timestamps.reserve(count);
	const auto start = TimePoint{};
	for (std::size_t i = 0; i < count; ++i) {
		timestamps.push_back(start + step * static_cast<long long>(i));
	}
	return timestamps;
}

inline anofoxtime::core::TimeSeries makeUnivariateSeries(std::vector<double> values) {
	auto timestamps = makeTimestamps(values.size());
	return anofoxtime::core::TimeSeries(std::move(timestamps), std::move(values));
}

inline anofoxtime::core::TimeSeries
makeMultivariateByColumns(std::vector<std::vector<double>> columns) {
	const std::size_t length = columns.empty() ? 0 : columns.front().size();
	auto timestamps = makeTimestamps(length);
	return anofoxtime::core::TimeSeries(std::move(timestamps), std::move(columns),
	                                    anofoxtime::core::TimeSeries::ValueLayout::ByColumn);
}

inline anofoxtime::core::TimeSeries makeMultivariateByRows(std::vector<std::vector<double>> rows) {
	auto timestamps = makeTimestamps(rows.size());
	return anofoxtime::core::TimeSeries(std::move(timestamps), std::move(rows),
	                                    anofoxtime::core::TimeSeries::ValueLayout::ByRow);
}

} // namespace tests::helpers
