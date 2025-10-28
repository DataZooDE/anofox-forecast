#pragma once

#include "anofox-time/detectors/ioutlier_detector.hpp"
#include "anofox-time/utils/logging.hpp"
#include <stdexcept>
#include <memory>

namespace anofoxtime::detectors {

class MADDetectorBuilder; // Forward declaration

/**
 * @class MADDetector
 * @brief An outlier detector based on the Median Absolute Deviation (MAD).
 *
 * MAD is a robust measure of variability and is resilient to the presence of outliers.
 */
class MADDetector final : public IOutlierDetector {
public:
	friend class MADDetectorBuilder;

	OutlierResult detect(const core::TimeSeries &ts) override;
	std::string getName() const override {
		return "MADDetector";
	}

private:
	/**
	 * @brief Private constructor for MADDetector.
	 * @param threshold The number of MADs away from the median.
	 */
	explicit MADDetector(double threshold);

	double threshold_;
};

/**
 * @class MADDetectorBuilder
 * @brief A builder for fluently configuring and creating MADDetector instances.
 */
class MADDetectorBuilder {
public:
	/**
	 * @brief Sets the threshold for outlier detection.
	 * @param threshold The number of MADs from the median.
	 * @return A reference to the builder for chaining.
	 */
	MADDetectorBuilder &withThreshold(double threshold);

	/**
	 * @brief Creates a new MADDetector instance.
	 * @return A unique pointer to the configured detector.
	 */
	std::unique_ptr<MADDetector> build();

private:
	double threshold_ = 3.5; // Default threshold
};

} // namespace anofoxtime::detectors
