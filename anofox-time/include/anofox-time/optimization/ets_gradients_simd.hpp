#pragma once

#include <vector>
#include <cstddef>

// SIMD acceleration for gradient computations
// Uses AVX2 when available, falls back to scalar

namespace anofoxtime::optimization {

/**
 * @brief SIMD-accelerated gradient operations
 * 
 * Provides vectorized implementations of common gradient operations
 * using AVX2 intrinsics when available.
 */
class ETSGradientsSIMD {
public:
    /**
     * @brief Vectorized gradient accumulation: out[i] += scale * in[i]
     * 
     * @param out Output vector (modified in-place)
     * @param in Input vector
     * @param scale Scalar multiplier
     * @param n Number of elements
     */
    static void vectorizedAccumulate(
        double* out,
        const double* in,
        double scale,
        size_t n
    );
    
    /**
     * @brief Vectorized gradient computation: out[i] = in[i] / sigma2
     * 
     * @param out Output vector
     * @param in Input vector (innovations)
     * @param sigma2 Variance
     * @param n Number of elements
     */
    static void vectorizedNormalize(
        double* out,
        const double* in,
        double sigma2,
        size_t n
    );
    
    /**
     * @brief Vectorized dot product: sum(a[i] * b[i])
     * 
     * @param a First vector
     * @param b Second vector  
     * @param n Number of elements
     * @return double Dot product
     */
    static double vectorizedDotProduct(
        const double* a,
        const double* b,
        size_t n
    );
    
    /**
     * @brief Check if AVX2 is available at runtime
     */
    static bool isAVX2Available();
    
private:
    // AVX2 implementations (used when available)
    static void vectorizedAccumulateAVX2(double* out, const double* in, double scale, size_t n);
    static void vectorizedNormalizeAVX2(double* out, const double* in, double sigma2, size_t n);
    static double vectorizedDotProductAVX2(const double* a, const double* b, size_t n);
    
    // Scalar fallback implementations
    static void vectorizedAccumulateScalar(double* out, const double* in, double scale, size_t n);
    static void vectorizedNormalizeScalar(double* out, const double* in, double sigma2, size_t n);
    static double vectorizedDotProductScalar(const double* a, const double* b, size_t n);
};

} // namespace anofoxtime::optimization



