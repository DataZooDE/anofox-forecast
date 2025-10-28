#include "anofox-time/optimization/ets_gradients_simd.hpp"
#include <cstdint>

#ifdef __AVX2__
#include <immintrin.h>
#endif

namespace anofoxtime::optimization {

// Runtime detection of AVX2 support
bool ETSGradientsSIMD::isAVX2Available() {
#ifdef __AVX2__
    // Check CPUID for AVX2 support
    uint32_t eax, ebx, ecx, edx;
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(7), "c"(0)
    );
    return (ebx & (1 << 5)) != 0;  // AVX2 bit
#else
    return false;
#endif
}

// ============================================================================
// Vectorized Accumulate: out[i] += scale * in[i]
// ============================================================================

#ifdef __AVX2__
void ETSGradientsSIMD::vectorizedAccumulateAVX2(double* out, const double* in, double scale, size_t n) {
    const size_t simd_width = 4;  // AVX2 processes 4 doubles at a time
    const size_t simd_n = (n / simd_width) * simd_width;
    
    // Broadcast scale to all lanes
    __m256d scale_vec = _mm256_set1_pd(scale);
    
    // Process 4 doubles at a time
    for (size_t i = 0; i < simd_n; i += simd_width) {
        // Load 4 doubles from input
        __m256d in_vec = _mm256_loadu_pd(&in[i]);
        
        // Load 4 doubles from output
        __m256d out_vec = _mm256_loadu_pd(&out[i]);
        
        // Multiply and accumulate: out = out + scale * in
        __m256d scaled = _mm256_mul_pd(scale_vec, in_vec);
        out_vec = _mm256_add_pd(out_vec, scaled);
        
        // Store result
        _mm256_storeu_pd(&out[i], out_vec);
    }
    
    // Handle remaining elements (scalar)
    for (size_t i = simd_n; i < n; ++i) {
        out[i] += scale * in[i];
    }
}
#endif

void ETSGradientsSIMD::vectorizedAccumulateScalar(double* out, const double* in, double scale, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        out[i] += scale * in[i];
    }
}

void ETSGradientsSIMD::vectorizedAccumulate(double* out, const double* in, double scale, size_t n) {
#ifdef __AVX2__
    if (isAVX2Available() && n >= 8) {  // Use AVX2 for larger arrays
        vectorizedAccumulateAVX2(out, in, scale, n);
    } else {
        vectorizedAccumulateScalar(out, in, scale, n);
    }
#else
    vectorizedAccumulateScalar(out, in, scale, n);
#endif
}

// ============================================================================
// Vectorized Normalize: out[i] = in[i] / sigma2
// ============================================================================

#ifdef __AVX2__
void ETSGradientsSIMD::vectorizedNormalizeAVX2(double* out, const double* in, double sigma2, size_t n) {
    const size_t simd_width = 4;
    const size_t simd_n = (n / simd_width) * simd_width;
    
    // Compute reciprocal once (more efficient than division)
    const double inv_sigma2 = 1.0 / sigma2;
    __m256d inv_vec = _mm256_set1_pd(inv_sigma2);
    
    // Process 4 doubles at a time
    for (size_t i = 0; i < simd_n; i += simd_width) {
        __m256d in_vec = _mm256_loadu_pd(&in[i]);
        __m256d result = _mm256_mul_pd(in_vec, inv_vec);
        _mm256_storeu_pd(&out[i], result);
    }
    
    // Handle remaining elements
    for (size_t i = simd_n; i < n; ++i) {
        out[i] = in[i] * inv_sigma2;
    }
}
#endif

void ETSGradientsSIMD::vectorizedNormalizeScalar(double* out, const double* in, double sigma2, size_t n) {
    const double inv_sigma2 = 1.0 / sigma2;
    for (size_t i = 0; i < n; ++i) {
        out[i] = in[i] * inv_sigma2;
    }
}

void ETSGradientsSIMD::vectorizedNormalize(double* out, const double* in, double sigma2, size_t n) {
#ifdef __AVX2__
    if (isAVX2Available() && n >= 8) {
        vectorizedNormalizeAVX2(out, in, sigma2, n);
    } else {
        vectorizedNormalizeScalar(out, in, sigma2, n);
    }
#else
    vectorizedNormalizeScalar(out, in, sigma2, n);
#endif
}

// ============================================================================
// Vectorized Dot Product: sum(a[i] * b[i])
// ============================================================================

#ifdef __AVX2__
double ETSGradientsSIMD::vectorizedDotProductAVX2(const double* a, const double* b, size_t n) {
    const size_t simd_width = 4;
    const size_t simd_n = (n / simd_width) * simd_width;
    
    // Accumulator for SIMD lanes
    __m256d sum_vec = _mm256_setzero_pd();
    
    // Process 4 doubles at a time
    for (size_t i = 0; i < simd_n; i += simd_width) {
        __m256d a_vec = _mm256_loadu_pd(&a[i]);
        __m256d b_vec = _mm256_loadu_pd(&b[i]);
        __m256d prod = _mm256_mul_pd(a_vec, b_vec);
        sum_vec = _mm256_add_pd(sum_vec, prod);
    }
    
    // Horizontal sum: reduce 4 lanes to single value
    // Extract high and low 128-bit halves
    __m128d sum_high = _mm256_extractf128_pd(sum_vec, 1);
    __m128d sum_low = _mm256_castpd256_pd128(sum_vec);
    __m128d sum128 = _mm_add_pd(sum_low, sum_high);
    
    // Shuffle and add to get final sum
    __m128d sum_shuf = _mm_shuffle_pd(sum128, sum128, 1);
    sum128 = _mm_add_pd(sum128, sum_shuf);
    
    double result = _mm_cvtsd_f64(sum128);
    
    // Handle remaining elements
    for (size_t i = simd_n; i < n; ++i) {
        result += a[i] * b[i];
    }
    
    return result;
}
#endif

double ETSGradientsSIMD::vectorizedDotProductScalar(const double* a, const double* b, size_t n) {
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

double ETSGradientsSIMD::vectorizedDotProduct(const double* a, const double* b, size_t n) {
#ifdef __AVX2__
    if (isAVX2Available() && n >= 8) {
        return vectorizedDotProductAVX2(a, b, n);
    } else {
        return vectorizedDotProductScalar(a, b, n);
    }
#else
    return vectorizedDotProductScalar(a, b, n);
#endif
}

} // namespace anofoxtime::optimization



