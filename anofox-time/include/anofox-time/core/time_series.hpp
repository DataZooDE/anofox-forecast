#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <limits>
#include <ctime>
#include <numeric>

namespace anofoxtime::core {

struct CalendarAnnotations {
	using TimePoint = std::chrono::system_clock::time_point;

	struct HolidayOccurrence {
		TimePoint start{};
		TimePoint end{};

		static HolidayOccurrence spanning(TimePoint start_time, TimePoint end_time) {
			if (end_time <= start_time) {
				throw std::invalid_argument("Holiday occurrence must have end strictly after start.");
			}
			return HolidayOccurrence{start_time, end_time};
		}

		static HolidayOccurrence forDay(TimePoint day_start) {
			return spanning(day_start, day_start + std::chrono::hours(24));
		}

		bool contains(const TimePoint &tp) const {
			return start <= tp && tp < end;
		}

		bool spansFullDay() const {
			return end - start >= std::chrono::hours(24);
		}
	};

	struct Holiday {
		std::vector<HolidayOccurrence> occurrences;
		std::optional<double> prior_scale;
	};

	enum class RegressorMode {
		Additive,
		Multiplicative
	};

	enum class RegressorStandardize {
		Auto,
		Yes,
		No
	};

	struct Regressor {
		std::vector<double> values;
		RegressorMode mode = RegressorMode::Additive;
		RegressorStandardize standardize = RegressorStandardize::Auto;
		std::optional<double> prior_scale;
	};

	std::string calendar_name;
	bool treat_weekends_as_holidays = true;
	std::unordered_map<std::string, Holiday> holidays;
	std::unordered_map<std::string, Regressor> regressors;

	void validate(std::size_t length) const {
		for (const auto &entry : regressors) {
			if (entry.second.values.size() != length) {
				throw std::invalid_argument("Regressor '" + entry.first + "' length must match time series length.");
			}
		}
		for (const auto &holiday_entry : holidays) {
			for (const auto &occurrence : holiday_entry.second.occurrences) {
				if (occurrence.end <= occurrence.start) {
					throw std::invalid_argument("Holiday occurrence for '" + holiday_entry.first +
					                            "' must have a positive duration.");
				}
			}
		}
	}

	bool empty() const {
		return calendar_name.empty() && holidays.empty() && regressors.empty();
	}
};

/**
 * @class TimeSeries
 * @brief Represents a sequence of data points over time.
 *
 * This class stores timestamps and corresponding values in separate vectors
 * for cache-efficient numerical processing. It ensures that the number of
 * timestamps always matches the number of values.
 */
class TimeSeries {
public:
	using TimePoint = std::chrono::system_clock::time_point;
	using Value = double;
	enum class ValueLayout { ByRow, ByColumn };
	using Metadata = std::unordered_map<std::string, std::string>;

	struct TimeZoneInfo {
		std::string name;
		std::optional<std::chrono::minutes> utc_offset;
	};

	enum class MissingValuePolicy {
		Error,
		Drop,
		FillValue,
		ForwardFill
	};

	struct Attributes {
		Metadata metadata;
		std::vector<Metadata> dimension_metadata;
		std::optional<TimeZoneInfo> timezone;
		std::optional<CalendarAnnotations> calendar;
	};

	struct SanitizeOptions {
		MissingValuePolicy policy = MissingValuePolicy::Error;
		double fill_value = 0.0;
	};

	enum class InterpolationMethod {
		Linear
	};

	struct InterpolationOptions {
		InterpolationMethod method = InterpolationMethod::Linear;
		double edge_fill_value = 0.0;
		bool fill_edges = true;
	};

	/**
	 * @brief Constructs a TimeSeries object.
	 * @param timestamps A vector of time points.
	 * @param values A vector of corresponding values.
	 * @throws std::invalid_argument If the sizes of timestamps and values vectors do not match.
	 */
	TimeSeries(std::vector<TimePoint> timestamps, std::vector<Value> values,
	           std::vector<std::string> labels = {}, Attributes attributes = {})
	    : timestamps_(std::move(timestamps)), values_by_dimension_(1) {
		if (timestamps_.size() != values.size()) {
			throw std::invalid_argument("Timestamps and values vectors must have the same size.");
		}
		values_by_dimension_[0] = std::move(values);
		validateTimestampOrder();
		if (!labels.empty() && labels.size() != 1) {
			throw std::invalid_argument("Labels must match the number of dimensions.");
		}
		initializeDimensionMetadata();
		labels_ = std::move(labels);
		applyAttributes(std::move(attributes));
	}

	TimeSeries(std::vector<TimePoint> timestamps, std::vector<std::vector<Value>> values,
	           ValueLayout layout, std::vector<std::string> labels = {}, Attributes attributes = {})
	    : timestamps_(std::move(timestamps)) {
		if (layout == ValueLayout::ByRow) {
			initializeFromRows(std::move(values));
		} else {
			initializeFromColumns(std::move(values));
		}
		validateTimestampOrder();
		if (!labels.empty() && labels.size() != values_by_dimension_.size()) {
			throw std::invalid_argument("Labels must match the number of dimensions.");
		}
		initializeDimensionMetadata();
		labels_ = std::move(labels);
		applyAttributes(std::move(attributes));
	}

	/**
	 * @brief Gets the timestamps.
	 * @return A const reference to the vector of timestamps.
	 */
	const std::vector<TimePoint> &getTimestamps() const {
		return timestamps_;
	}

	/**
	 * @brief Gets the primary value series (dimension 0).
	 * @return A const reference to the vector of values for the first dimension.
	 */
	const std::vector<Value> &getValues() const {
		if (values_by_dimension_.empty()) {
			throw std::runtime_error("TimeSeries contains no value dimensions.");
		}
		return values_by_dimension_.front();
	}

	/**
	 * @brief Gets the values for a specific dimension.
	 */
	const std::vector<Value> &getValues(std::size_t dimension) const {
		if (dimension >= values_by_dimension_.size()) {
			throw std::out_of_range("Requested dimension exceeds the number of value dimensions.");
		}
		return values_by_dimension_[dimension];
	}

	/**
	 * @brief Gets the values.
	 * @return A const reference to the vector of values.
	 */
	[[deprecated("Use getValues() or getValues(dimension) for explicit access.")]]
	const std::vector<Value> &values() const {
		return getValues();
	}

	/**
	 * @brief Returns all values arranged by dimension.
	 */
	const std::vector<std::vector<Value>> &getValuesByDimension() const {
		return values_by_dimension_;
	}

	/**
	 * @brief Extracts a single observation across all dimensions.
	 */
	std::vector<Value> getRow(std::size_t index) const {
		if (index >= size()) {
			throw std::out_of_range("Requested observation exceeds the time series length.");
		}
		std::vector<Value> row;
		row.reserve(values_by_dimension_.size());
		for (const auto &dimension : values_by_dimension_) {
			if (dimension.size() != size()) {
				throw std::logic_error("Inconsistent dimension length detected.");
			}
			row.push_back(dimension[index]);
		}
		return row;
	}

	/**
	 * @brief Returns the number of value dimensions.
	 */
	std::size_t dimensions() const {
		return values_by_dimension_.size();
	}

	bool isMultivariate() const {
		return dimensions() > 1;
	}

	const std::vector<std::string> &labels() const {
		return labels_;
	}

	void setLabels(std::vector<std::string> labels) {
		if (!labels.empty() && labels.size() != dimensions()) {
			throw std::invalid_argument("Labels must match the number of dimensions.");
		}
		labels_ = std::move(labels);
	}

	const Metadata &metadata() const {
		return metadata_;
	}

	void setMetadata(Metadata metadata) {
		metadata_ = std::move(metadata);
	}

	const Metadata &dimensionMetadata(std::size_t dimension) const {
		if (dimension >= dimensions()) {
			throw std::out_of_range("Requested dimension exceeds the number of value dimensions.");
		}
		return dimension_metadata_[dimension];
	}

	void setDimensionMetadata(std::size_t dimension, Metadata metadata) {
		if (dimension >= dimensions()) {
			throw std::out_of_range("Requested dimension exceeds the number of value dimensions.");
		}
		dimension_metadata_[dimension] = std::move(metadata);
	}

	const std::vector<Metadata> &allDimensionMetadata() const {
		return dimension_metadata_;
	}

	void setDimensionMetadata(std::vector<Metadata> metadata) {
		validateDimensionMetadataSize(metadata);
		dimension_metadata_ = std::move(metadata);
	}

	std::optional<std::chrono::nanoseconds> frequency() const {
		return frequency_;
	}

	void setFrequency(std::chrono::nanoseconds frequency) {
		frequency_ = frequency;
	}

	void clearFrequency() {
		frequency_.reset();
	}

	std::optional<std::chrono::nanoseconds> inferFrequency(std::chrono::nanoseconds tolerance = std::chrono::nanoseconds{0}) const {
		if (timestamps_.size() < 2) {
			return std::nullopt;
		}

		const auto normalized_tolerance =
		    (tolerance >= std::chrono::nanoseconds::zero()) ? tolerance : -tolerance;

		std::vector<std::chrono::nanoseconds> differences;
		differences.reserve(timestamps_.size() - 1);
		for (std::size_t i = 0; i + 1 < timestamps_.size(); ++i) {
			auto raw_diff = timestamps_[i + 1] - timestamps_[i];
			if (raw_diff <= std::chrono::nanoseconds::zero()) {
				return std::nullopt;
			}
			auto adjusted = adjustedDiff(timestamps_[i], timestamps_[i + 1]);
			if (adjusted <= std::chrono::nanoseconds::zero()) {
				adjusted = raw_diff;
			}
			if (adjusted <= std::chrono::nanoseconds::zero()) {
				return std::nullopt;
			}
			differences.push_back(adjusted);
		}

		const auto base_diff = differences.front();
		const bool within_tolerance =
		    std::all_of(differences.begin() + 1, differences.end(), [&](const auto &diff) {
			    const auto delta = diff > base_diff ? diff - base_diff : base_diff - diff;
			    return delta <= normalized_tolerance;
		    });
		if (within_tolerance) {
			return base_diff;
		}

		const std::size_t max_samples = 5;
		const std::size_t start_index =
		    differences.size() > max_samples ? differences.size() - max_samples : 0;
		struct Cluster {
			std::int64_t canonical = 0;
			std::size_t count = 0;
		};
		std::vector<Cluster> clusters;
		clusters.reserve(max_samples);
		const auto tolerance_count = normalized_tolerance.count();
		for (std::size_t i = start_index; i < differences.size(); ++i) {
			const auto diff_count = differences[i].count();
			bool assigned = false;
			for (auto &cluster : clusters) {
				const auto delta = (diff_count >= cluster.canonical) ? diff_count - cluster.canonical
				                                                     : cluster.canonical - diff_count;
				if (delta <= tolerance_count) {
					++cluster.count;
					assigned = true;
					break;
				}
			}
			if (!assigned) {
				clusters.push_back(Cluster{diff_count, 1});
			}
		}

		if (clusters.empty()) {
			return std::nullopt;
		}

		std::size_t best_index = 0;
		std::size_t best_count = clusters[0].count;
		bool unique_best = true;
		for (std::size_t i = 1; i < clusters.size(); ++i) {
			const auto count = clusters[i].count;
			if (count > best_count) {
				best_index = i;
				best_count = count;
				unique_best = true;
			} else if (count == best_count) {
				unique_best = false;
			}
		}

		if (!unique_best) {
			return std::nullopt;
		}

		const auto candidate = clusters[best_index].canonical;
		if (candidate <= 0) {
			return std::nullopt;
		}
		return std::chrono::nanoseconds(candidate);
	}

	bool setFrequencyFromTimestamps(std::chrono::nanoseconds tolerance = std::chrono::nanoseconds{0}) {
		auto inferred = inferFrequency(tolerance);
		if (!inferred) {
			return false;
		}
		frequency_ = inferred;
		return true;
	}


	Attributes attributes() const {
	Attributes attrs;
	attrs.metadata = metadata_;
	attrs.dimension_metadata = dimension_metadata_;
	attrs.timezone = timezone_;
	attrs.calendar = calendar_;
	return attrs;
}

	const std::optional<TimeZoneInfo> &timezone() const {
		return timezone_;
	}

	void setTimezone(TimeZoneInfo timezone) {
		validateTimezone(timezone);
		timezone_ = std::move(timezone);
	}

	void clearTimezone() {
		timezone_.reset();
	}

	bool hasCalendar() const {
		return calendar_.has_value();
	}

	const CalendarAnnotations &calendarAnnotations() const {
		if (!calendar_) {
			throw std::logic_error("TimeSeries has no calendar annotations.");
		}
		return *calendar_;
	}

	void setCalendar(CalendarAnnotations annotations) {
		if (annotations.empty()) {
			calendar_.reset();
			holiday_days_.clear();
			return;
		}
		annotations.validate(size());
		calendar_ = std::move(annotations);
		rebuildHolidayIndex();
	}

	void clearCalendar() {
		calendar_.reset();
		holiday_days_.clear();
	}

	bool isHoliday(const TimePoint &tp) const {
		if (!calendar_) {
			return false;
		}
		for (const auto &holiday_entry : calendar_->holidays) {
			for (const auto &occurrence : holiday_entry.second.occurrences) {
				if (occurrence.contains(tp)) {
					return true;
				}
			}
		}
		return isCalendarHolidayDay(dayKey(tp));
	}

	bool isBusinessDay(const TimePoint &tp) const {
		return !isHoliday(tp);
	}

	bool hasRegressors() const {
		return calendar_ && !calendar_->regressors.empty();
	}

	const std::unordered_map<std::string, CalendarAnnotations::Regressor> &regressors() const {
		return calendar_ ? calendar_->regressors : emptyRegressors();
	}

	const CalendarAnnotations::Regressor &regressorDefinition(const std::string &name) const {
		if (!calendar_) {
			throw std::out_of_range("TimeSeries has no calendar regressors.");
		}
		const auto it = calendar_->regressors.find(name);
		if (it == calendar_->regressors.end()) {
			throw std::out_of_range("Regressor '" + name + "' not found.");
		}
		return it->second;
	}

	const std::vector<double> &regressor(const std::string &name) const {
		return regressorDefinition(name).values;
	}

	/**
	 * @brief Gets the number of data points in the series.
	 * @return The size of the time series.
	 */
	size_t size() const {
		return timestamps_.size();
	}

	/**
	 * @brief Checks if the time series is empty.
	 * @return True if the time series contains no data points, false otherwise.
	 */
	bool isEmpty() const {
		return size() == 0;
	}

	TimeSeries slice(std::size_t start, std::size_t end) const {
		if (start > end) {
			throw std::invalid_argument("Slice start index must not exceed end index.");
		}
		if (end > size()) {
			throw std::out_of_range("Slice end index exceeds the length of the time series.");
		}

		std::vector<TimePoint> sliced_timestamps;
		sliced_timestamps.reserve(end - start);
		sliced_timestamps.insert(sliced_timestamps.end(), timestamps_.begin() + static_cast<std::ptrdiff_t>(start),
		                         timestamps_.begin() + static_cast<std::ptrdiff_t>(end));

	std::vector<std::vector<Value>> sliced_columns;
	sliced_columns.reserve(dimensions());
	for (const auto &dimension : values_by_dimension_) {
		sliced_columns.emplace_back(dimension.begin() + static_cast<std::ptrdiff_t>(start),
		                            dimension.begin() + static_cast<std::ptrdiff_t>(end));
	}

	std::vector<std::size_t> indices(end - start);
	for (std::size_t i = 0; i < indices.size(); ++i) {
		indices[i] = start + i;
	}
	Attributes attrs = buildAttributesForIndices(indices, sliced_timestamps);

	TimeSeries result(std::move(sliced_timestamps), std::move(sliced_columns), ValueLayout::ByColumn, labels_,
	                  std::move(attrs));
	if (frequency_) {
		result.setFrequency(*frequency_);
	}
	return result;
}

	bool hasMissingValues() const {
		for (const auto &dimension : values_by_dimension_) {
			for (double v : dimension) {
				if (!std::isfinite(v)) {
					return true;
				}
			}
		}
		return false;
	}

	TimeSeries sanitized() const {
		return sanitized(SanitizeOptions{});
	}

	TimeSeries sanitized(const SanitizeOptions &options) const {
		switch (options.policy) {
		case MissingValuePolicy::Error:
			if (hasMissingValues()) {
				throw std::invalid_argument("TimeSeries contains non-finite values.");
			}
			return *this;
		case MissingValuePolicy::Drop:
			return sanitizedDrop();
		case MissingValuePolicy::FillValue:
			return sanitizedFill(options.fill_value);
		case MissingValuePolicy::ForwardFill:
			return sanitizedForwardFill(options.fill_value);
		default:
			throw std::logic_error("Unsupported missing value policy.");
		}
	}

	TimeSeries interpolated() const {
		return interpolated(InterpolationOptions{});
	}

	TimeSeries interpolated(const InterpolationOptions &options) const {
		if (!hasMissingValues()) {
			return *this;
		}

		std::vector<std::vector<Value>> new_values = values_by_dimension_;
		switch (options.method) {
		case InterpolationMethod::Linear:
			for (auto &dimension : new_values) {
				interpolateLinear(dimension, options);
			}
			break;
		default:
			throw std::invalid_argument("Unsupported interpolation method.");
		}

		TimeSeries result(timestamps_, std::move(new_values), ValueLayout::ByColumn, labels_, attributes());
		if (frequency_) {
			result.setFrequency(*frequency_);
		}
		return result;
	}

private:
	void initializeFromRows(std::vector<std::vector<Value>> rows) {
		if (rows.size() != timestamps_.size()) {
			throw std::invalid_argument("Row-major values must match the number of timestamps.");
		}
		if (rows.empty()) {
			values_by_dimension_.clear();
			return;
		}
		const auto dimension_count = rows.front().size();
		for (const auto &row : rows) {
			if (row.size() != dimension_count) {
				throw std::invalid_argument("All rows must have the same number of dimensions.");
			}
		}
		values_by_dimension_.assign(dimension_count, std::vector<Value>(rows.size()));
		for (std::size_t i = 0; i < rows.size(); ++i) {
			for (std::size_t j = 0; j < dimension_count; ++j) {
				values_by_dimension_[j][i] = rows[i][j];
			}
		}
	}

	void initializeFromColumns(std::vector<std::vector<Value>> columns) {
		if (columns.empty()) {
			values_by_dimension_.clear();
			return;
		}
		for (const auto &column : columns) {
			if (column.size() != timestamps_.size()) {
				throw std::invalid_argument("Column-major values must align with the number of timestamps.");
			}
		}
		values_by_dimension_ = std::move(columns);
	}

	void initializeDimensionMetadata() {
		dimension_metadata_.assign(values_by_dimension_.size(), Metadata{});
	}

	void applyAttributes(Attributes attributes) {
	if (!attributes.dimension_metadata.empty()) {
		validateDimensionMetadataSize(attributes.dimension_metadata);
		dimension_metadata_ = std::move(attributes.dimension_metadata);
	}
	if (attributes.timezone) {
		validateTimezone(*attributes.timezone);
		timezone_ = std::move(attributes.timezone);
	} else {
		timezone_.reset();
	}
	if (attributes.calendar) {
		setCalendar(std::move(*attributes.calendar));
	} else {
		clearCalendar();
	}
	metadata_ = std::move(attributes.metadata);
}

	void validateDimensionMetadataSize(const std::vector<Metadata> &metadata) const {
		if (!metadata.empty() && metadata.size() != dimensions()) {
			throw std::invalid_argument("Dimension metadata must match the number of value dimensions.");
		}
	}

	static void validateTimezone(const TimeZoneInfo &timezone) {
		if (timezone.name.empty()) {
			throw std::invalid_argument("Timezone name must not be empty.");
		}
		if (timezone.utc_offset) {
			const auto offset = timezone.utc_offset->count();
			const auto min_offset = -24 * 60;
			const auto max_offset = 24 * 60;
			if (offset < min_offset || offset > max_offset) {
				throw std::invalid_argument("Timezone UTC offset must be within [-24h, 24h].");
			}
		}
	}

	std::vector<TimePoint> timestamps_;
	std::vector<std::vector<Value>> values_by_dimension_;
	std::vector<std::string> labels_;
	std::optional<std::chrono::nanoseconds> frequency_;
	Metadata metadata_;
	std::vector<Metadata> dimension_metadata_;
	std::optional<TimeZoneInfo> timezone_;
	std::optional<CalendarAnnotations> calendar_;
	std::unordered_set<std::int64_t> holiday_days_;
	static constexpr std::int64_t kSecondsPerDay = 86400LL;
	static constexpr std::int64_t kNanosecondsPerDay = 86400LL * 1000000000LL;

	TimeSeries sanitizedDrop() const {
		std::vector<std::size_t> keep_indices;
		keep_indices.reserve(size());
		for (std::size_t i = 0; i < size(); ++i) {
			bool finite = true;
			for (const auto &dimension : values_by_dimension_) {
				if (!std::isfinite(dimension[i])) {
					finite = false;
					break;
				}
			}
			if (finite) {
				keep_indices.push_back(i);
			}
		}

		std::vector<TimePoint> new_timestamps;
		new_timestamps.reserve(keep_indices.size());
		for (auto index : keep_indices) {
			new_timestamps.push_back(timestamps_[index]);
		}

		std::vector<std::vector<Value>> new_values;
		new_values.reserve(values_by_dimension_.size());
		for (const auto &dimension : values_by_dimension_) {
			std::vector<Value> column;
			column.reserve(keep_indices.size());
			for (auto index : keep_indices) {
				column.push_back(dimension[index]);
			}
			new_values.push_back(std::move(column));
		}

		Attributes attrs;
		attrs.metadata = metadata_;
		attrs.dimension_metadata = dimension_metadata_;
		attrs.timezone = timezone_;
		if (!keep_indices.empty()) {
			attrs = buildAttributesForIndices(keep_indices, new_timestamps);
		}

		TimeSeries result(
		    std::move(new_timestamps), std::move(new_values), ValueLayout::ByColumn, labels_, std::move(attrs));
		if (frequency_) {
			result.setFrequency(*frequency_);
		}
		return result;
	}

	TimeSeries sanitizedFill(double fill_value) const {
		std::vector<std::vector<Value>> new_values = values_by_dimension_;
		for (auto &dimension : new_values) {
			for (auto &value : dimension) {
				if (!std::isfinite(value)) {
					value = fill_value;
				}
			}
		}
		TimeSeries result(timestamps_, std::move(new_values), ValueLayout::ByColumn, labels_, attributes());
		if (frequency_) {
			result.setFrequency(*frequency_);
		}
		return result;
	}

	TimeSeries sanitizedForwardFill(double initial_fill) const {
		std::vector<std::vector<Value>> new_values = values_by_dimension_;
		for (auto &dimension : new_values) {
			double last_valid = initial_fill;
			bool has_last = false;
			for (auto &value : dimension) {
				if (std::isfinite(value)) {
					last_valid = value;
					has_last = true;
				} else if (has_last) {
					value = last_valid;
				} else {
					value = initial_fill;
				}
			}
		}
		TimeSeries result(timestamps_, std::move(new_values), ValueLayout::ByColumn, labels_, attributes());
		if (frequency_) {
			result.setFrequency(*frequency_);
		}
		return result;
	}

	Attributes buildAttributesForIndices(const std::vector<std::size_t> &indices,
	                                     const std::vector<TimePoint> &new_timestamps) const {
		Attributes attrs;
		attrs.metadata = metadata_;
		attrs.dimension_metadata = dimension_metadata_;
		attrs.timezone = timezone_;
		if (calendar_) {
			CalendarAnnotations cal = *calendar_;
			for (auto &entry : cal.regressors) {
				const auto source_it = calendar_->regressors.find(entry.first);
				if (source_it == calendar_->regressors.end()) {
					entry.second.values.clear();
					continue;
				}
				const auto &source_values = source_it->second.values;
				std::vector<double> subset;
				subset.reserve(indices.size());
				for (auto idx : indices) {
					if (idx >= source_values.size()) {
						throw std::out_of_range("Regressor index out of range.");
					}
					subset.push_back(source_values[idx]);
				}
				entry.second.values = std::move(subset);
			}
			if (!new_timestamps.empty()) {
				const auto start_tp = new_timestamps.front();
				const auto end_tp = new_timestamps.back();
				for (auto &holiday_entry : cal.holidays) {
					std::vector<CalendarAnnotations::HolidayOccurrence> filtered;
					for (const auto &occurrence : holiday_entry.second.occurrences) {
						if (occurrence.end <= start_tp || occurrence.start > end_tp) {
							continue;
						}
						filtered.push_back(occurrence);
					}
					holiday_entry.second.occurrences = std::move(filtered);
				}
			} else {
				for (auto &holiday_entry : cal.holidays) {
					holiday_entry.second.occurrences.clear();
				}
			}
			cal.validate(new_timestamps.size());
			attrs.calendar = std::move(cal);
		}
		return attrs;
	}

	void validateTimestampOrder() const {
		if (timestamps_.empty()) {
			return;
		}
		for (std::size_t i = 1; i < timestamps_.size(); ++i) {
			if (!(timestamps_[i] > timestamps_[i - 1])) {
				throw std::invalid_argument("TimeSeries timestamps must be strictly increasing and unique.");
			}
		}
	}

	template <typename Clock, typename Duration>
	static std::int64_t dayKey(const std::chrono::time_point<Clock, Duration> &tp) {
		static constexpr std::int64_t denom = kNanosecondsPerDay;
		const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
		std::int64_t quotient = ns / denom;
		const std::int64_t remainder = ns % denom;
		if (remainder < 0) {
			--quotient;
		}
		return quotient;
	}

	static bool safeGmTime(std::time_t time_value, std::tm &out) {
#if defined(_WIN32)
		return gmtime_s(&out, &time_value) == 0;
#else
		std::tm *result = std::gmtime(&time_value);
		if (!result) {
			return false;
		}
		out = *result;
		return true;
#endif
	}

	bool isWeekendDay(std::int64_t day_key) const {
		if (!calendar_ || !calendar_->treat_weekends_as_holidays) {
			return false;
		}
		std::tm tm{};
		std::time_t seconds = static_cast<std::time_t>(day_key * kSecondsPerDay);
		if (!safeGmTime(seconds, tm)) {
			return false;
		}
		int wday = tm.tm_wday;
		return wday == 0 || wday == 6;
	}

	bool isCalendarHolidayDay(std::int64_t day_key) const {
		if (!calendar_) {
			return false;
		}
		if (holiday_days_.find(day_key) != holiday_days_.end()) {
			return true;
		}
		return isWeekendDay(day_key);
	}

	void rebuildHolidayIndex() {
		holiday_days_.clear();
		if (!calendar_) {
			return;
		}
		for (const auto &holiday_entry : calendar_->holidays) {
			for (const auto &occurrence : holiday_entry.second.occurrences) {
				if (!occurrence.spansFullDay()) {
					continue;
				}
				const auto start_day = dayKey(occurrence.start);
				const auto end_ns = occurrence.end - std::chrono::nanoseconds{1};
				auto end_day = dayKey(end_ns);
				if (end_day < start_day) {
					end_day = start_day;
				}
				for (auto day = start_day; day <= end_day; ++day) {
					holiday_days_.insert(day);
				}
			}
		}
	}

	std::chrono::nanoseconds adjustedDiff(const TimePoint &prev, const TimePoint &curr) const {
		auto diff = curr - prev;
		if (!calendar_ || diff <= std::chrono::nanoseconds::zero()) {
			return diff;
		}
		const auto prev_day = dayKey(prev);
		const auto curr_day = dayKey(curr);
		if (curr_day <= prev_day) {
			return diff;
		}
		std::chrono::nanoseconds adjustment{0};
		for (auto day = prev_day + 1; day < curr_day; ++day) {
			if (isCalendarHolidayDay(day)) {
				adjustment += std::chrono::nanoseconds(kNanosecondsPerDay);
			}
		}
		auto adjusted = diff - adjustment;
		if (adjusted <= std::chrono::nanoseconds::zero()) {
			return diff;
		}
		return adjusted;
	}

	static const std::unordered_map<std::string, CalendarAnnotations::Regressor> &emptyRegressors() {
		static const std::unordered_map<std::string, CalendarAnnotations::Regressor> empty;
		return empty;
	}

	static void interpolateLinear(std::vector<Value> &dimension, const InterpolationOptions &options) {
		const std::size_t n = dimension.size();
		if (n == 0) {
			return;
		}

		std::size_t idx = 0;
		while (idx < n) {
			if (std::isfinite(dimension[idx])) {
				++idx;
				continue;
			}

			std::size_t run_start = idx;
			while (run_start > 0 && !std::isfinite(dimension[run_start - 1])) {
				--run_start;
			}
			bool has_previous = run_start > 0 && std::isfinite(dimension[run_start - 1]);
			std::size_t prev_idx = has_previous ? run_start - 1 : run_start;
			double prev_value = has_previous ? dimension[prev_idx] : options.edge_fill_value;

			std::size_t run_end = idx;
			while (run_end < n && !std::isfinite(dimension[run_end])) {
				++run_end;
			}
			bool has_next = run_end < n && std::isfinite(dimension[run_end]);
			double next_value = has_next ? dimension[run_end] : options.edge_fill_value;

			if (has_previous && has_next) {
				const std::size_t gap = run_end - prev_idx;
				for (std::size_t step = 1; step < gap; ++step) {
					double ratio = static_cast<double>(step) / static_cast<double>(gap);
					dimension[prev_idx + step] = prev_value + (next_value - prev_value) * ratio;
				}
			} else if (has_previous && !has_next) {
				double fill = options.fill_edges ? options.edge_fill_value : prev_value;
				for (std::size_t k = idx; k < n; ++k) {
					dimension[k] = fill;
				}
				break;
			} else if (!has_previous && has_next) {
				double fill = options.fill_edges ? options.edge_fill_value : next_value;
				for (std::size_t k = 0; k < run_end; ++k) {
					dimension[k] = fill;
				}
			} else {
				double fill = options.edge_fill_value;
				for (auto &value : dimension) {
					if (!std::isfinite(value)) {
						value = fill;
					}
				}
				break;
			}

			idx = run_end;
		}
	}
};

} // namespace anofoxtime::core
