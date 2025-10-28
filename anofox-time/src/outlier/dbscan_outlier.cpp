#include "anofox-time/outlier/dbscan_outlier.hpp"

#include "anofox-time/utils/logging.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

using anofoxtime::utils::Logging;

namespace anofoxtime::outlier {

DbscanOutlierDetector::DbscanOutlierDetector(
    std::unique_ptr<clustering::DbscanClusterer> clusterer)
    : clusterer_(std::move(clusterer)) {}

OutlierDetectionResult DbscanOutlierDetector::detect(const core::DistanceMatrix &matrix) const {
	const auto assignments = clusterer_->cluster(matrix);
	const auto n = assignments.size();

	std::unordered_map<std::uint32_t, std::size_t> cluster_counts;
	cluster_counts.reserve(n);
	for (const auto &assignment : assignments) {
		if (assignment.isCluster()) {
			++cluster_counts[assignment.id()];
		}
	}

	std::uint32_t dominant_cluster = 0;
	std::size_t dominant_count = 0;
	for (const auto &[cluster_id, count] : cluster_counts) {
		if (count > dominant_count) {
			dominant_cluster = cluster_id;
			dominant_count = count;
		}
	}

#ifndef ANOFOX_NO_LOGGING
	auto logger = Logging::getLogger();
	if (logger->should_log(spdlog::level::trace)) {
		logger->trace("DBSCAN outlier detection dominant cluster={} size={}",
		              dominant_cluster == 0 ? -1 : static_cast<int>(dominant_cluster),
		              dominant_count);
	}
#endif

	OutlierDetectionResult result;
	result.series_results.resize(n);

	for (std::size_t i = 0; i < n; ++i) {
		const auto &assignment = assignments[i];
		auto &series_result = result.series_results[i];
		series_result.scores.resize(1, 0.0);

		const bool is_outlier =
		    assignment.isNoise() || (assignment.isCluster() && assignment.id() != dominant_cluster);
		series_result.is_outlier = is_outlier;
		series_result.scores[0] = is_outlier ? 1.0 : 0.0;
		if (is_outlier) {
			result.outlying_series.push_back(i);
		}
	}

	return result;
}

DbscanOutlierBuilder &DbscanOutlierBuilder::withEpsilon(double epsilon) {
	if (epsilon < 0.0) {
		throw std::invalid_argument("epsilon must be non-negative");
	}
	epsilon_ = epsilon;
	return *this;
}

DbscanOutlierBuilder &DbscanOutlierBuilder::withMinClusterSize(std::size_t min_cluster_size) {
	if (min_cluster_size < 1) {
		throw std::invalid_argument("min_cluster_size must be at least 1");
	}
	min_cluster_size_ = min_cluster_size;
	return *this;
}

std::unique_ptr<DbscanOutlierDetector> DbscanOutlierBuilder::build() const {
	clustering::DbscanBuilder cluster_builder;
	cluster_builder.withEpsilon(epsilon_);
	cluster_builder.withMinClusterSize(min_cluster_size_);
	return std::unique_ptr<DbscanOutlierDetector>(
	    new DbscanOutlierDetector(cluster_builder.build()));
}

} // namespace anofoxtime::outlier
