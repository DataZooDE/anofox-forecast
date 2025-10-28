#pragma once

#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

namespace anofoxtime::core {

/**
 * @class DistanceMatrix
 * @brief Represents a symmetric square matrix of pairwise distances.
 *
 * The matrix is stored as a vector-of-vectors to favour cache-friendly access
 * patterns during neighbourhood lookups (e.g. DBSCAN expansion). The class
 * enforces the matrix to be square at construction time and offers lightweight
 * iteration helpers that mirror the ergonomics of the original Rust API.
 */
class DistanceMatrix {
public:
	using Matrix = std::vector<std::vector<double>>;
	using Row = std::vector<double>;

	DistanceMatrix() = default;

	/**
	 * @brief Construct a distance matrix from a square matrix.
	 * @throws std::invalid_argument if the matrix is not square.
	 */
	explicit DistanceMatrix(Matrix data) : matrix_(std::move(data)) {
		validateSquare();
	}

	/**
	 * @brief Create a distance matrix from a square matrix.
	 * @param data The matrix to take ownership of.
	 * @return A DistanceMatrix instance if validation succeeds.
	 * @throws std::invalid_argument if the matrix is not square.
	 */
	static DistanceMatrix fromSquare(Matrix data) {
		return DistanceMatrix(std::move(data));
	}

	/**
	 * @brief Returns the number of rows/columns in the matrix.
	 */
	std::size_t size() const noexcept {
		return matrix_.size();
	}

	/**
	 * @brief Returns whether the matrix is empty.
	 */
	bool empty() const noexcept {
		return matrix_.empty();
	}

	/**
	 * @brief Returns the matrix dimensions as (rows, columns).
	 */
	std::pair<std::size_t, std::size_t> shape() const noexcept {
		const auto n = matrix_.size();
		return {n, n};
	}

	/**
	 * @brief Mutable row access.
	 */
	Row &operator[](std::size_t index) {
		return matrix_[index];
	}

	/**
	 * @brief Const row access.
	 */
	const Row &operator[](std::size_t index) const {
		return matrix_[index];
	}

	/**
	 * @brief Const element access.
	 */
	const double &at(std::size_t row, std::size_t col) const {
		return matrix_.at(row).at(col);
	}

	/**
	 * @brief Mutable element access.
	 */
	double &at(std::size_t row, std::size_t col) {
		return matrix_.at(row).at(col);
	}

	/**
	 * @brief Iterator support for range-for compatibility.
	 */
	Matrix::const_iterator begin() const noexcept {
		return matrix_.begin();
	}
	Matrix::const_iterator end() const noexcept {
		return matrix_.end();
	}

	Matrix::iterator begin() noexcept {
		return matrix_.begin();
	}
	Matrix::iterator end() noexcept {
		return matrix_.end();
	}

	/**
	 * @brief Returns the underlying matrix by const reference.
	 */
	const Matrix &data() const noexcept {
		return matrix_;
	}

private:
	void validateSquare() const {
		const auto n = matrix_.size();
		for (const auto &row : matrix_) {
			if (row.size() != n) {
				throw std::invalid_argument("distance matrix must be square");
			}
		}
	}

	Matrix matrix_;
};

} // namespace anofoxtime::core

