#pragma once

#include "anofox-time/core/distance_matrix.hpp"
#include "anofox-time/utils/logging.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace anofoxtime::clustering {

/**
 * @class DbscanCluster
 * @brief Represents the cluster assignment for a single observation.
 *
 * The cluster identifier follows the same conventions as the Rust implementation:
 * `0` denotes noise while positive identifiers represent cluster membership. The
 * helper methods expose the label as either an integer (`-1` for noise) or a
 * strongly typed identifier.
 */
class DbscanCluster {
public:
	DbscanCluster() = default;

	/**
	 * @brief Construct a cluster assignment for a dense cluster.
	 * @throws std::invalid_argument if id == 0.
	 */
	static DbscanCluster cluster(std::uint32_t id) {
		if (id == 0) {
			throw std::invalid_argument("cluster id must be greater than zero");
		}
		return DbscanCluster(id);
	}

	/**
	 * @brief Construct a noise assignment.
	 */
	static DbscanCluster noise() noexcept {
		return DbscanCluster(0);
	}

	/**
	 * @brief Returns true if this assignment denotes noise.
	 */
	[[nodiscard]] bool isNoise() const noexcept {
		return id_ == 0;
	}

	/**
	 * @brief Returns true if this assignment denotes a cluster.
	 */
	[[nodiscard]] bool isCluster() const noexcept {
		return id_ != 0;
	}

	/**
	 * @brief Returns the cluster identifier.
	 * @throws std::logic_error if called on a noise assignment.
	 */
	[[nodiscard]] std::uint32_t id() const {
		if (isNoise()) {
			throw std::logic_error("noise assignments do not have a cluster id");
		}
		return id_;
	}

	/**
	 * @brief Convert to the integer label format used by the quick API and legacy code.
	 */
	[[nodiscard]] int label() const noexcept {
		return isNoise() ? -1 : static_cast<int>(id_);
	}

	/**
	 * @brief Equality comparison.
	 */
	bool operator==(const DbscanCluster &other) const noexcept {
		return id_ == other.id_;
	}

private:
	explicit DbscanCluster(std::uint32_t id) : id_(id) {}

	std::uint32_t id_ = 0;
};

/**
 * @class DbscanClusterer
 * @brief Density-based clustering using pairwise distances.
 *
 * The implementation relies on a precomputed distance matrix, making it ideal for
 * reusing DTW or other bespoke similarity measures.
 */
class DbscanClusterer {
public:
	friend class DbscanBuilder;

	DbscanClusterer(const DbscanClusterer &) = delete;
	DbscanClusterer &operator=(const DbscanClusterer &) = delete;
	DbscanClusterer(DbscanClusterer &&) noexcept = default;
	DbscanClusterer &operator=(DbscanClusterer &&) noexcept = default;

	/**
	 * @brief Cluster the items represented by the distance matrix.
	 * @return A vector of cluster identifiers.
	 */
	[[nodiscard]] std::vector<DbscanCluster> cluster(const core::DistanceMatrix &matrix) const;

	/**
	 * @brief Convenience helper returning the legacy integer labels.
	 */
	[[nodiscard]] std::vector<int> clusterLabels(const core::DistanceMatrix &matrix) const;

	double epsilon() const noexcept {
		return epsilon_;
	}

	std::size_t minClusterSize() const noexcept {
		return min_cluster_size_;
	}

private:
	DbscanClusterer(double epsilon, std::size_t min_cluster_size);

	void findNeighbours(std::size_t point,
	                    const core::DistanceMatrix::Row &distances,
	                    std::vector<std::size_t> &buffer) const;

	double epsilon_;
	std::size_t min_cluster_size_;
};

/**
 * @class DbscanBuilder
 * @brief Fluent builder for configuring DBSCAN.
 */
class DbscanBuilder {
public:
	DbscanBuilder &withEpsilon(double epsilon);
	DbscanBuilder &withMinClusterSize(std::size_t min_cluster_size);

	std::unique_ptr<DbscanClusterer> build() const;

private:
	double epsilon_ = 1.0;
	std::size_t min_cluster_size_ = 5;
};

} // namespace anofoxtime::clustering
