#include "residual.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

// Enable SIMD hints only on ARM with NEON
#ifdef ENABLE_NEON
    #define SIMD_HINT _Pragma("omp simd")
#else
    #define SIMD_HINT
#endif

namespace lwir {

void compute_residual(
    const uint16_t* __restrict current,
    const uint16_t* __restrict previous,
    int16_t* __restrict residual,
    size_t pixel_count)
{
    // Use SIMD hints for compiler auto-vectorization
    SIMD_HINT
    for (size_t i = 0; i < pixel_count; ++i) {
        // Cast to signed to handle negative differences
        residual[i] = static_cast<int16_t>(current[i]) - static_cast<int16_t>(previous[i]);
    }
}

void quantize_residual(
    const int16_t* __restrict residual,
    int16_t* __restrict quantized,
    size_t pixel_count,
    const QuantizationParams& params)
{
    const uint32_t T = params.dead_zone_T;
    const uint32_t Q_fixed = params.quant_Q_fixed;
    const uint32_t fp_bits = params.fp_bits;
    const uint32_t rounding = (1u << (fp_bits - 1));  // For round-half-up

    SIMD_HINT
    for (size_t i = 0; i < pixel_count; ++i) {
        const int16_t R = residual[i];
        const int32_t sign = (R >= 0) ? 1 : -1;
        const uint32_t abs_R = static_cast<uint32_t>(std::abs(static_cast<int32_t>(R)));

        // Dead-zone: a2 = max(0, |R| - T)
        const uint32_t a2 = (abs_R > T) ? (abs_R - T) : 0;

        // Quantize: q = round(a2 / Q) using fixed-point arithmetic
        // q = (a2 * invQ + 2^(fp_bits-1)) >> fp_bits
        // where invQ = 2^fp_bits / Q_fixed (computed as: (1 << fp_bits) / Q)
        // But we can simplify: a2/Q ~= (a2 << fp_bits) / Q_fixed
        const uint32_t numerator = (a2 << fp_bits) + rounding;
        const uint32_t q_abs = numerator / Q_fixed;

        quantized[i] = static_cast<int16_t>(sign * static_cast<int32_t>(q_abs));
    }
}

void dequantize_residual(
    const int16_t* __restrict quantized,
    int16_t* __restrict reconstructed,
    size_t pixel_count,
    const QuantizationParams& params)
{
    const uint32_t T = params.dead_zone_T;
    const uint32_t Q_fixed = params.quant_Q_fixed;
    const uint32_t fp_bits = params.fp_bits;
    const uint32_t T_half = T / 2;  // Centered reconstruction

    SIMD_HINT
    for (size_t i = 0; i < pixel_count; ++i) {
        const int16_t q = quantized[i];

        if (q == 0) {
            reconstructed[i] = 0;
        } else {
            const int32_t sign = (q >= 0) ? 1 : -1;
            const uint32_t abs_q = static_cast<uint32_t>(std::abs(static_cast<int32_t>(q)));

            // R_hat = |q| * Q + T/2
            const uint32_t recon_abs = ((abs_q * Q_fixed) >> fp_bits) + T_half;

            reconstructed[i] = static_cast<int16_t>(sign * static_cast<int32_t>(recon_abs));
        }
    }
}

void bias_residual(
    const int16_t* __restrict residual,
    uint16_t* __restrict biased,
    size_t pixel_count,
    int16_t bias_offset)
{
    SIMD_HINT
    for (size_t i = 0; i < pixel_count; ++i) {
        // Map [-1024, +1023] → [0, 2047]
        biased[i] = static_cast<uint16_t>(residual[i] + bias_offset);
    }
}

void unbias_residual(
    const uint16_t* __restrict biased,
    int16_t* __restrict residual,
    size_t pixel_count,
    int16_t bias_offset)
{
    SIMD_HINT
    for (size_t i = 0; i < pixel_count; ++i) {
        // Map [0, 2047] → [-1024, +1023]
        residual[i] = static_cast<int16_t>(biased[i] - bias_offset);
    }
}

void reconstruct_frame(
    const int16_t* __restrict residual,
    const uint16_t* __restrict previous,
    uint16_t* __restrict reconstructed,
    size_t pixel_count)
{
    SIMD_HINT
    for (size_t i = 0; i < pixel_count; ++i) {
        // I_t = R_t + I_{t-1}, with clamping to [0, 65535]
        const int32_t val = static_cast<int32_t>(previous[i]) + static_cast<int32_t>(residual[i]);
        reconstructed[i] = static_cast<uint16_t>(std::clamp(val, 0, 65535));
    }
}

ErrorStats compute_error_stats(
    const uint16_t* __restrict original,
    const uint16_t* __restrict reconstructed,
    size_t pixel_count)
{
    ErrorStats stats;

    if (pixel_count == 0) {
        return stats;
    }

    double sum_error = 0.0;
    double sum_sq_error = 0.0;
    double max_err = 0.0;

    for (size_t i = 0; i < pixel_count; ++i) {
        const double err = std::abs(static_cast<double>(original[i]) -
                                   static_cast<double>(reconstructed[i]));
        sum_error += err;
        sum_sq_error += err * err;
        max_err = std::max(max_err, err);
    }

    stats.mean_error = sum_error / pixel_count;
    stats.rmse = std::sqrt(sum_sq_error / pixel_count);
    stats.max_error = max_err;

    return stats;
}

} // namespace lwir
