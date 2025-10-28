#pragma once

#include "anofox-time/clustering/dbscan.hpp"
#include "anofox-time/core/distance_matrix.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace anofoxtime::outlier {

struct OutlierSeriesResult {
	bool is_outlier = false;
	std::vector<double> scores;
};

struct OutlierDetectionResult {
	std::vector<std::size_t> outlying_series;
	std::vector<OutlierSeriesResult> series_results;
};

class DbscanOutlierDetector {
public:
	friend class DbscanOutlierBuilder;

	DbscanOutlierDetector(const DbscanOutlierDetector &) = delete;
	DbscanOutlierDetector &operator=(const DbscanOutlierDetector &) = delete;
	DbscanOutlierDetector(DbscanOutlierDetector &&) noexcept = default;
	DbscanOutlierDetector &operator=(DbscanOutlierDetector &&) noexcept = default;

	OutlierDetectionResult detect(const core::DistanceMatrix &matrix) const;

private:
	explicit DbscanOutlierDetector(std::unique_ptr<clustering::DbscanClusterer> clusterer);

	std::unique_ptr<clustering::DbscanClusterer> clusterer_;
};

class DbscanOutlierBuilder {
public:
	DbscanOutlierBuilder &withEpsilon(double epsilon);
	DbscanOutlierBuilder &withMinClusterSize(std::size_t min_cluster_size);

	std::unique_ptr<DbscanOutlierDetector> build() const;

private:
	double epsilon_ = 1.0;
	std::size_t min_cluster_size_ = 5;
};

} // namespace anofoxtime::outlier
