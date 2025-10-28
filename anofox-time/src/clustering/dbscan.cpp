#include "anofox-time/clustering/dbscan.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>

using anofoxtime::core::DistanceMatrix;
using anofoxtime::utils::Logging;

namespace anofoxtime::clustering {

DbscanClusterer::DbscanClusterer(double epsilon, std::size_t min_cluster_size)
    : epsilon_(epsilon), min_cluster_size_(min_cluster_size) {}

std::vector<DbscanCluster> DbscanClusterer::cluster(const DistanceMatrix &matrix) const {
	const auto n = matrix.size();
	std::vector<DbscanCluster> clusters(n, DbscanCluster::noise());
	std::vector<bool> visited(n, false);
	std::vector<std::size_t> neighbours;
	neighbours.reserve(n);
	std::deque<std::size_t> to_visit;
	std::uint32_t next_cluster_id = 1;

#ifndef ANOFOX_NO_LOGGING
	auto logger = Logging::getLogger();
	if (logger->should_log(spdlog::level::trace)) {
		logger->trace("DBSCAN clustering start: epsilon={} min_cluster_size={} points={}",
		              epsilon_, min_cluster_size_, n);
	}
#endif

	for (std::size_t i = 0; i < n; ++i) {
		if (clusters[i].isCluster()) {
			continue;
		}
		findNeighbours(i, matrix[i], neighbours);
		if (neighbours.size() + 1 < min_cluster_size_) {
#ifndef ANOFOX_NO_LOGGING
			if (logger->should_log(spdlog::level::trace)) {
				logger->trace("DBSCAN point {} treated as noise: neighbourhood too small (size={})",
				              i, neighbours.size() + 1);
			}
#endif
			continue;
		}

		if (next_cluster_id == std::numeric_limits<std::uint32_t>::max()) {
#ifndef ANOFOX_NO_LOGGING
			logger->error("DBSCAN cluster id overflow");
#endif
			throw std::overflow_error("DBSCAN cluster id overflow");
		}

		const auto current_cluster = DbscanCluster::cluster(next_cluster_id);
#ifndef ANOFOX_NO_LOGGING
		if (logger->should_log(spdlog::level::trace)) {
			logger->trace("DBSCAN creating cluster {} from core point {}", next_cluster_id, i);
		}
#endif

		visited[i] = true;
		clusters[i] = current_cluster;

		to_visit.clear();
		for (auto neighbour : neighbours) {
			if (clusters[neighbour].isNoise()) {
				visited[neighbour] = true;
				to_visit.push_back(neighbour);
			}
		}

		while (!to_visit.empty()) {
			const auto candidate = to_visit.front();
			to_visit.pop_front();
			clusters[candidate] = current_cluster;

			findNeighbours(candidate, matrix[candidate], neighbours);
			if (neighbours.size() + 1 >= min_cluster_size_) {
				for (auto neighbour : neighbours) {
					if (!visited[neighbour]) {
						visited[neighbour] = true;
						to_visit.push_back(neighbour);
					}
				}
			}
		}

		++next_cluster_id;
	}

	return clusters;
}

std::vector<int> DbscanClusterer::clusterLabels(const DistanceMatrix &matrix) const {
	const auto assignments = cluster(matrix);
	std::vector<int> labels;
	labels.reserve(assignments.size());
	std::transform(assignments.begin(), assignments.end(), std::back_inserter(labels),
	               [](const DbscanCluster &cluster) { return cluster.label(); });
	return labels;
}

void DbscanClusterer::findNeighbours(std::size_t point,
                                     const DistanceMatrix::Row &distances,
                                     std::vector<std::size_t> &buffer) const {
	buffer.clear();
	for (std::size_t j = 0; j < distances.size(); ++j) {
		if (j == point) {
			continue;
		}
		if (distances[j] <= epsilon_) {
			buffer.push_back(j);
		}
	}
}

DbscanBuilder &DbscanBuilder::withEpsilon(double epsilon) {
	if (epsilon < 0.0) {
		throw std::invalid_argument("epsilon must be non-negative");
	}
	epsilon_ = epsilon;
	return *this;
}

DbscanBuilder &DbscanBuilder::withMinClusterSize(std::size_t min_cluster_size) {
	if (min_cluster_size < 1) {
		throw std::invalid_argument("min_cluster_size must be at least 1");
	}
	min_cluster_size_ = min_cluster_size;
	return *this;
}

std::unique_ptr<DbscanClusterer> DbscanBuilder::build() const {
	return std::unique_ptr<DbscanClusterer>(new DbscanClusterer(epsilon_, min_cluster_size_));
}

} // namespace anofoxtime::clustering
