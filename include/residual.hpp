#pragma once

#include <cstdint>
#include <vector>
#include <cstdlib>
#include "frame.hpp"

namespace lwir {

/**
 * Quantization parameters for residual encoding
 */
struct QuantizationParams {
    uint32_t dead_zone_T;     // Dead-zone threshold (integer DN)
    uint32_t quant_Q_fixed;   // Quantization step in fixed-point (Q * 2^fp_bits)
    uint32_t fp_bits;         // Number of fractional bits (default: 8)

    QuantizationParams(uint32_t T = 2, double Q = 2.0, uint32_t fp = 8)
        : dead_zone_T(T), fp_bits(fp) {
        quant_Q_fixed = static_cast<uint32_t>(Q * (1u << fp_bits) + 0.5);
    }

    double get_Q() const {
        return static_cast<double>(quant_Q_fixed) / (1u << fp_bits);
    }
};

/**
 * Compute temporal residual: R = current - previous
 * Output is int16_t since residuals can be negative
 */
void compute_residual(
    const uint16_t* __restrict current,
    const uint16_t* __restrict previous,
    int16_t* __restrict residual,
    size_t pixel_count
);

/**
 * Quantize residual with dead-zone and quantization step
 * Formula: a = |R|; a2 = max(0, a - T); q = sign(R) * round(a2 / Q)
 *
 * Uses fixed-point arithmetic for fractional Q
 */
void quantize_residual(
    const int16_t* __restrict residual,
    int16_t* __restrict quantized,
    size_t pixel_count,
    const QuantizationParams& params
);

/**
 * Dequantize residual
 * Formula: R_hat = sign(q) * (|q| * Q + T/2)
 *
 * Uses centered reconstruction to minimize bias
 */
void dequantize_residual(
    const int16_t* __restrict quantized,
    int16_t* __restrict reconstructed,
    size_t pixel_count,
    const QuantizationParams& params
);

/**
 * Bias residual to unsigned range for CharLS encoding
 * Maps [-1024, +1023] → [0, 2047] by adding 1024
 */
void bias_residual(
    const int16_t* __restrict residual,
    uint16_t* __restrict biased,
    size_t pixel_count,
    int16_t bias_offset = 1024
);

/**
 * Unbias residual from unsigned to signed
 * Maps [0, 2047] → [-1024, +1023] by subtracting 1024
 */
void unbias_residual(
    const uint16_t* __restrict biased,
    int16_t* __restrict residual,
    size_t pixel_count,
    int16_t bias_offset = 1024
);

/**
 * Reconstruct frame from residual and previous frame
 * I_t = R_t + I_{t-1}
 */
void reconstruct_frame(
    const int16_t* __restrict residual,
    const uint16_t* __restrict previous,
    uint16_t* __restrict reconstructed,
    size_t pixel_count
);

/**
 * Add residual to reference frame (alias for reconstruct_frame)
 */
inline void add_residual_to_reference(
    const uint16_t* __restrict reference,
    const int16_t* __restrict residual,
    uint16_t* __restrict output,
    size_t pixel_count)
{
    reconstruct_frame(residual, reference, output, pixel_count);
}

/**
 * Compute error statistics between original and reconstructed
 */
struct ErrorStats {
    double max_error;
    double mean_error;
    double rmse;

    ErrorStats() : max_error(0.0), mean_error(0.0), rmse(0.0) {}
};

ErrorStats compute_error_stats(
    const uint16_t* __restrict original,
    const uint16_t* __restrict reconstructed,
    size_t pixel_count
);

} // namespace lwir
