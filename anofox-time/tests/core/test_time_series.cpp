#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/core/time_series.hpp"
#include "common/time_series_helpers.hpp"

#include <chrono>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

using anofoxtime::core::CalendarAnnotations;
using anofoxtime::core::TimeSeries;

TEST_CASE("TimeSeries constructs univariate data", "[core][time_series]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0});

	REQUIRE(series.size() == 3);
	REQUIRE_FALSE(series.isEmpty());
	REQUIRE(series.dimensions() == 1);
	REQUIRE_FALSE(series.isMultivariate());

	const auto &values = series.getValues();
	REQUIRE(values.size() == 3);
	REQUIRE(values[1] == Catch::Approx(2.0));

	series.setLabels({"close"});
	REQUIRE(series.labels() == std::vector<std::string>{"close"});
	REQUIRE_THROWS_AS(series.setLabels({"a", "b"}), std::invalid_argument);

	series.setFrequency(std::chrono::seconds(60));
	REQUIRE(series.frequency().has_value());
    REQUIRE(*series.frequency() == std::chrono::seconds(60));
	series.clearFrequency();
	REQUIRE_FALSE(series.frequency().has_value());

	series.setMetadata({{"source", "sensor-A"}});
	REQUIRE(series.metadata().at("source") == "sensor-A");

	series.setDimensionMetadata(0, {{"unit", "USD"}});
	REQUIRE(series.dimensionMetadata(0).at("unit") == "USD");

	TimeSeries::TimeZoneInfo tz{"UTC", std::chrono::minutes{0}};
	series.setTimezone(tz);
	REQUIRE(series.timezone().has_value());
	REQUIRE(series.timezone()->name == "UTC");
	series.clearTimezone();
	REQUIRE_FALSE(series.timezone().has_value());
}

TEST_CASE("TimeSeries handles multivariate column layout", "[core][time_series][multivariate]") {
	auto series = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0}, {10.0, 20.0, 30.0}});

	REQUIRE(series.dimensions() == 2);
	REQUIRE(series.isMultivariate());
	REQUIRE(series.size() == 3);

	const auto &first_dimension = series.getValues(0);
	const auto &second_dimension = series.getValues(1);
	REQUIRE(first_dimension[2] == Catch::Approx(3.0));
	REQUIRE(second_dimension[1] == Catch::Approx(20.0));

	const auto row = series.getRow(1);
	REQUIRE(row.size() == 2);
	REQUIRE(row[0] == Catch::Approx(2.0));
	REQUIRE(row[1] == Catch::Approx(20.0));

	REQUIRE_THROWS_AS(series.getRow(3), std::out_of_range);
}

TEST_CASE("TimeSeries handles multivariate row layout", "[core][time_series][layout]") {
	TimeSeries::TimePoint base{};
	std::vector<TimeSeries::TimePoint> timestamps{base, base + std::chrono::seconds(1)};
	std::vector<std::vector<double>> rows{{1.0, 10.0}, {2.0, 20.0}};

	TimeSeries series(std::move(timestamps), rows, TimeSeries::ValueLayout::ByRow);
	REQUIRE(series.dimensions() == 2);
	REQUIRE(series.size() == 2);

	const auto &dim1 = series.getValues(0);
	const auto &dim2 = series.getValues(1);
	REQUIRE(dim1[0] == Catch::Approx(1.0));
	REQUIRE(dim2[1] == Catch::Approx(20.0));
}

TEST_CASE("TimeSeries validates constructor input", "[core][time_series][validation]") {
	auto timestamps = tests::helpers::makeTimestamps(2);
	std::vector<double> fewer_values{1.0};
	REQUIRE_THROWS_AS(TimeSeries(timestamps, fewer_values), std::invalid_argument);

	std::vector<std::vector<double>> inconsistent_rows{{1.0, 2.0}, {3.0}};
	REQUIRE_THROWS_AS(TimeSeries(timestamps, inconsistent_rows, TimeSeries::ValueLayout::ByRow),
	                  std::invalid_argument);

	std::vector<std::vector<double>> inconsistent_columns{{1.0, 2.0}, {3.0}};
	REQUIRE_THROWS_AS(TimeSeries(timestamps, inconsistent_columns, TimeSeries::ValueLayout::ByColumn),
	                  std::invalid_argument);

	const auto timestamps_single = tests::helpers::makeTimestamps(1);
	std::vector<std::string> invalid_labels{"a", "b"};
	REQUIRE_THROWS_AS(TimeSeries(timestamps_single, std::vector<double>{1.0}, invalid_labels),
	                  std::invalid_argument);
}

TEST_CASE("TimeSeries rejects non-increasing timestamps", "[core][time_series][validation]") {
	auto timestamps = tests::helpers::makeTimestamps(3);
	std::vector<double> values{1.0, 2.0, 3.0};
	timestamps[1] = timestamps[0];
	REQUIRE_THROWS_AS(TimeSeries(timestamps, values), std::invalid_argument);

	auto descending = tests::helpers::makeTimestamps(3);
	std::swap(descending[1], descending[2]);
	REQUIRE_THROWS_AS(TimeSeries(descending, values), std::invalid_argument);
}

TEST_CASE("TimeSeries stores metadata and timezone attributes", "[core][time_series][metadata]") {
	auto timestamps = tests::helpers::makeTimestamps(2);
	std::vector<double> values{1.0, 2.0};

	TimeSeries::Attributes attrs;
	attrs.metadata.emplace("source", "sensor-A");
	attrs.metadata.emplace("location", "plant-1");
	attrs.dimension_metadata = {TimeSeries::Metadata{{"unit", "kW"}, {"channel", "L1"}}};
	attrs.timezone = TimeSeries::TimeZoneInfo{"America/New_York", std::chrono::minutes{-300}};

	TimeSeries series(std::move(timestamps), std::move(values), {"power"}, std::move(attrs));

	REQUIRE(series.metadata().at("location") == "plant-1");
	REQUIRE(series.labels() == std::vector<std::string>{"power"});
	REQUIRE(series.dimensionMetadata(0).at("unit") == "kW");
	REQUIRE(series.timezone()->name == "America/New_York");
	REQUIRE(series.timezone()->utc_offset == std::chrono::minutes{-300});

	auto all_meta = series.allDimensionMetadata();
	REQUIRE(all_meta.size() == 1);
	REQUIRE(all_meta.front().at("channel") == "L1");

	TimeSeries::Attributes invalid_attrs;
	invalid_attrs.dimension_metadata = {TimeSeries::Metadata{}, TimeSeries::Metadata{}};
	REQUIRE_THROWS_AS(TimeSeries(tests::helpers::makeTimestamps(2), std::vector<double>{3.0, 4.0},
	                            {"invalid"}, invalid_attrs),
	                  std::invalid_argument);

	REQUIRE_THROWS_AS(series.setTimezone(TimeSeries::TimeZoneInfo{"", std::nullopt}),
	                  std::invalid_argument);
	REQUIRE_THROWS_AS(series.setTimezone(TimeSeries::TimeZoneInfo{"UTC", std::chrono::minutes{24 * 60 + 1}}),
	                  std::invalid_argument);
}

TEST_CASE("TimeSeries slice preserves dimensional metadata", "[core][time_series][slice]") {
	auto series = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0, 4.0}, {10.0, 20.0, 30.0, 40.0}});
	series.setLabels({"primary", "secondary"});
	series.setMetadata({{"source", "unit-test"}});
	series.setDimensionMetadata({TimeSeries::Metadata{{"unit", "A"}}, TimeSeries::Metadata{{"unit", "B"}}});
	series.setTimezone(TimeSeries::TimeZoneInfo{"UTC", std::chrono::minutes{0}});
	series.setFrequency(std::chrono::seconds{60});

	const auto attributes_before = series.attributes();
	REQUIRE(attributes_before.metadata.at("source") == "unit-test");

	const auto sliced = series.slice(1, 3);
	REQUIRE(sliced.size() == 2);
	REQUIRE(sliced.dimensions() == 2);
	REQUIRE(sliced.labels() == std::vector<std::string>{"primary", "secondary"});
	REQUIRE(sliced.metadata().at("source") == "unit-test");
	REQUIRE(sliced.dimensionMetadata(0).at("unit") == "A");
	REQUIRE(sliced.timezone()->name == "UTC");
	REQUIRE(sliced.frequency() == std::optional<std::chrono::nanoseconds>(std::chrono::seconds{60}));
	REQUIRE(sliced.getValues(0)[0] == Catch::Approx(2.0));
	REQUIRE_THROWS_AS(series.slice(3, 2), std::invalid_argument);
	REQUIRE_THROWS_AS(series.slice(0, 5), std::out_of_range);
}

TEST_CASE("TimeSeries sanitizes missing values", "[core][time_series][sanitize]") {
	TimeSeries::TimePoint base{};
	std::vector<TimeSeries::TimePoint> timestamps{base, base + std::chrono::seconds(1), base + std::chrono::seconds(2)};
	std::vector<std::vector<double>> columns{{1.0, std::numeric_limits<double>::quiet_NaN(), 3.0},
	                                       {10.0, 20.0, std::numeric_limits<double>::infinity()}};

	TimeSeries series(timestamps, columns, TimeSeries::ValueLayout::ByColumn, {"a", "b"});
	REQUIRE(series.hasMissingValues());

	// Drop policy removes any row with non-finite values
	auto dropped = series.sanitized({TimeSeries::MissingValuePolicy::Drop});
	REQUIRE(dropped.size() == 1);
	REQUIRE_FALSE(dropped.hasMissingValues());
	REQUIRE(dropped.getValues(0)[0] == Catch::Approx(1.0));

	// Fill policy replaces with provided fill value
	auto filled = series.sanitized({TimeSeries::MissingValuePolicy::FillValue, 42.0});
	REQUIRE(filled.size() == series.size());
	REQUIRE_FALSE(filled.hasMissingValues());
	REQUIRE(filled.getValues(0)[1] == Catch::Approx(42.0));
	REQUIRE(filled.getValues(1)[2] == Catch::Approx(42.0));

	// Forward fill policy propagates previous values
	auto forward = series.sanitized({TimeSeries::MissingValuePolicy::ForwardFill, 0.0});
	REQUIRE(forward.getValues(0)[1] == Catch::Approx(1.0));
	REQUIRE(forward.getValues(1)[2] == Catch::Approx(20.0));
	REQUIRE_FALSE(forward.hasMissingValues());

	// Error policy throws when missing values present
	REQUIRE_THROWS_AS(series.sanitized({TimeSeries::MissingValuePolicy::Error}), std::invalid_argument);
}

TEST_CASE("TimeSeries calendar annotations manage holidays and regressors", "[core][time_series][calendar]") {
	using TimePoint = TimeSeries::TimePoint;
	const auto day = std::chrono::hours(24);
	TimePoint base = TimePoint{};
	std::vector<TimePoint> timestamps{
	    base,
	    base + day,
	    base + day * 2,
	    base + day * 3,
	    base + day * 4,
	};
	std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};

	TimeSeries series(timestamps, values);

	CalendarAnnotations calendar;
	calendar.calendar_name = "BUSINESS";
	calendar.treat_weekends_as_holidays = true;
	CalendarAnnotations::Holiday shutdown;
	shutdown.occurrences.push_back(CalendarAnnotations::HolidayOccurrence::forDay(base + day * 3));
	calendar.holidays.emplace("inventory_shutdown", std::move(shutdown));
	CalendarAnnotations::Regressor promotion;
	promotion.values = {0.0, 1.0, 0.0, 1.0, 0.0};
	calendar.regressors.emplace("promotion", std::move(promotion));

	series.setCalendar(calendar);

	REQUIRE(series.hasCalendar());
	REQUIRE(series.calendarAnnotations().calendar_name == "BUSINESS");
	REQUIRE(series.hasRegressors());
	REQUIRE(series.regressor("promotion").size() == series.size());
	const auto &promotion_def = series.regressorDefinition("promotion");
	REQUIRE(promotion_def.mode == CalendarAnnotations::RegressorMode::Additive);
	REQUIRE(promotion_def.standardize == CalendarAnnotations::RegressorStandardize::Auto);
	REQUIRE(series.isHoliday(series.getTimestamps()[3]));
	REQUIRE_FALSE(series.isHoliday(series.getTimestamps()[1]));

	auto sliced = series.slice(1, 4);
	REQUIRE(sliced.hasCalendar());
	REQUIRE(sliced.regressor("promotion").size() == 3);
	REQUIRE(sliced.isHoliday(sliced.getTimestamps()[2]));

	std::vector<double> values_with_nan{1.0, std::numeric_limits<double>::quiet_NaN(), 3.0, 4.0, 5.0};
	TimeSeries with_nan(timestamps, values_with_nan);
	with_nan.setCalendar(calendar);
	auto dropped = with_nan.sanitized({TimeSeries::MissingValuePolicy::Drop});
	REQUIRE(dropped.size() == 4);
	REQUIRE(dropped.hasCalendar());
	REQUIRE(dropped.regressor("promotion").size() == 4);
}

TEST_CASE("Calendar-aware frequency inference skips weekends", "[core][time_series][calendar][frequency]") {
	using TimePoint = TimeSeries::TimePoint;
	const auto day = std::chrono::hours(24);
	TimePoint friday = TimePoint{} + day; // 1970-01-02 Friday
	TimePoint monday = friday + day * 3;   // skip weekend
	TimePoint tuesday = monday + day;

	std::vector<TimePoint> timestamps{friday, monday, tuesday};
	std::vector<double> values{10.0, 20.0, 30.0};

	TimeSeries series(timestamps, values);
	REQUIRE_FALSE(series.inferFrequency());

	CalendarAnnotations calendar;
	calendar.calendar_name = "BUSINESS";
	calendar.treat_weekends_as_holidays = true;
	CalendarAnnotations::Regressor constant;
	constant.values = {1.0, 1.0, 1.0};
	calendar.regressors.emplace("const", std::move(constant));
	series.setCalendar(calendar);

	auto frequency = series.inferFrequency();
	REQUIRE(frequency.has_value());
	REQUIRE(*frequency == std::chrono::hours(24));
	REQUIRE(series.setFrequencyFromTimestamps());

	const auto &const_def = series.regressorDefinition("const");
	REQUIRE(const_def.mode == CalendarAnnotations::RegressorMode::Additive);
}

TEST_CASE("TimeSeries linear interpolation fills gaps", "[core][time_series][interpolate]") {
	TimeSeries::TimePoint base{};
	std::vector<TimeSeries::TimePoint> timestamps{base, base + std::chrono::seconds(1), base + std::chrono::seconds(2),
	                                             base + std::chrono::seconds(3), base + std::chrono::seconds(4)};
	std::vector<std::vector<double>> columns{{1.0, std::numeric_limits<double>::quiet_NaN(),
	                                         std::numeric_limits<double>::quiet_NaN(), 4.0, 5.0}};

	TimeSeries series(timestamps, columns, TimeSeries::ValueLayout::ByColumn);
	auto interpolated = series.interpolated();
	const auto &values = interpolated.getValues();
	REQUIRE(values[1] == Catch::Approx(2.0).margin(1e-6));
	REQUIRE(values[2] == Catch::Approx(3.0).margin(1e-6));

	// Leading gap with edge fill
	std::vector<std::vector<double>> leading{{std::numeric_limits<double>::quiet_NaN(), 2.0, 3.0}};
	std::vector<TimeSeries::TimePoint> leading_timestamps{timestamps.begin(), timestamps.begin() + 3};
	TimeSeries leading_series(leading_timestamps, leading, TimeSeries::ValueLayout::ByColumn);
	TimeSeries::InterpolationOptions opts;
	opts.edge_fill_value = 42.0;
	auto leading_filled = leading_series.interpolated(opts);
	REQUIRE(leading_filled.getValues()[0] == Catch::Approx(42.0));

	opts.fill_edges = false;
	auto leading_hold = leading_series.interpolated(opts);
	REQUIRE(leading_hold.getValues()[0] == Catch::Approx(2.0));
}

TEST_CASE("TimeSeries infers regular frequency", "[core][time_series][frequency]") {
	auto timestamps = tests::helpers::makeTimestamps(5, std::chrono::seconds(60));
	std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
	TimeSeries series(timestamps, values);

	auto inferred = series.inferFrequency();
	REQUIRE(inferred.has_value());
	REQUIRE(*inferred == std::chrono::seconds(60));

	series.clearFrequency();
	REQUIRE(series.setFrequencyFromTimestamps());
	REQUIRE(series.frequency().has_value());
	REQUIRE(*series.frequency() == std::chrono::seconds(60));

	// Irregular timestamps should fail unless tolerance supplied
	auto irregular = timestamps;
	irregular[4] = irregular[3] + std::chrono::seconds(75);
	TimeSeries irregular_series(irregular, values);
	auto modal = irregular_series.inferFrequency();
	REQUIRE(modal.has_value());
	REQUIRE(*modal == std::chrono::seconds(60));
	REQUIRE(irregular_series.setFrequencyFromTimestamps());
	auto tolerant = irregular_series.inferFrequency(std::chrono::seconds(30));
	REQUIRE(tolerant.has_value());
	REQUIRE(*tolerant == std::chrono::seconds(60));
}

TEST_CASE("TimeSeries frequency inference requires unique modal spacing", "[core][time_series][frequency][mode]") {
	std::vector<TimeSeries::TimePoint> timestamps;
	const auto base = TimeSeries::TimePoint{};
	timestamps.push_back(base);
	timestamps.push_back(base + std::chrono::seconds(60));
	timestamps.push_back(base + std::chrono::seconds(120));
	timestamps.push_back(base + std::chrono::seconds(150));
	timestamps.push_back(base + std::chrono::seconds(180));

	std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
	TimeSeries series(timestamps, values);

	auto inferred = series.inferFrequency();
	REQUIRE_FALSE(inferred.has_value());
	REQUIRE_FALSE(series.setFrequencyFromTimestamps());

	auto tolerant = series.inferFrequency(std::chrono::seconds(31));
	REQUIRE(tolerant.has_value());
	REQUIRE(*tolerant == std::chrono::seconds(60));
}

TEST_CASE("TimeSeries detects partial-day holiday occurrences", "[core][time_series][calendar][holiday]") {
	using TimePoint = TimeSeries::TimePoint;
	TimePoint base{};
	std::vector<TimePoint> timestamps{base,
	                                  base + std::chrono::hours(1),
	                                  base + std::chrono::hours(2),
	                                  base + std::chrono::hours(3)};
	std::vector<double> values{1.0, 2.0, 3.0, 4.0};

	TimeSeries series(timestamps, values);

	CalendarAnnotations calendar;
	CalendarAnnotations::Holiday maintenance;
	maintenance.occurrences.push_back(CalendarAnnotations::HolidayOccurrence::spanning(base + std::chrono::minutes(30),
	                                                                                   base + std::chrono::minutes(90)));
	calendar.holidays.emplace("maintenance", std::move(maintenance));
	series.setCalendar(calendar);

	REQUIRE(series.isHoliday(base + std::chrono::minutes(45)));
	REQUIRE_FALSE(series.isHoliday(base + std::chrono::hours(2)));

	auto inferred = series.inferFrequency();
	REQUIRE(inferred.has_value());
	REQUIRE(*inferred == std::chrono::hours(1));
}
