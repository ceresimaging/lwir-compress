#include "decision.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace lwir {

FrameDecisionEngine::FrameDecisionEngine(const DecisionState& initial_state)
    : state_(initial_state)
{
}

FrameMode FrameDecisionEngine::decide_mode(const ResidualStats& stats, uint32_t frame_index) {
    // 0) Periodic cap / max GOP
    if (should_force_periodic(frame_index)) {
        return FrameMode::USE_INTRA;
    }

    // 1) Heuristics (scene change, FFC/NUC, etc.)
    if (should_force_heuristic(stats)) {
        return FrameMode::USE_INTRA;
    }

    // 2) Entropy proxy vs intra EMA (with hysteresis)
    if (should_use_intra_rate(stats)) {
        // 3) Optional tile probe if close (not implemented in MVP)
        if (state_.enable_probe &&
            std::abs(stats.bps_res - state_.bps_intra_ema) < state_.probe_band_bpp) {
            // TODO: Implement tile probe
            // For now, just use rate decision
        }
        return FrameMode::USE_INTRA;
    }

    return FrameMode::USE_RESIDUAL;
}

void FrameDecisionEngine::update_intra_bpp(size_t keyframe_bytes, uint32_t width, uint32_t height) {
    double bpp = (keyframe_bytes * 8.0) / (width * height);
    state_.bps_intra_ema = (1.0 - state_.ema_alpha) * state_.bps_intra_ema +
                           state_.ema_alpha * bpp;
    state_.frames_since_key = 0;
    state_.last_mode = FrameMode::USE_INTRA;
}

void FrameDecisionEngine::mark_residual() {
    state_.frames_since_key++;
    state_.last_mode = FrameMode::USE_RESIDUAL;
}

bool FrameDecisionEngine::should_force_periodic(uint32_t frame_index) const {
    // Force keyframe every GOP period or at hard cap
    return (frame_index % state_.gop_period == 0) ||
           (state_.frames_since_key >= state_.gop_max);
}

bool FrameDecisionEngine::should_force_heuristic(const ResidualStats& stats) const {
    // Any of these conditions triggers an intra frame
    if (stats.zero_mass < state_.zero_mass_min) {
        return true;  // Too few small changes
    }
    if (stats.mean_abs > state_.mean_abs_max) {
        return true;  // Big per-pixel change
    }
    if (stats.p95 > state_.p95_max || stats.p99 > state_.p99_max) {
        return true;  // Heavy tails (scene change, FFC/NUC)
    }
    return false;
}

bool FrameDecisionEngine::should_use_intra_rate(const ResidualStats& stats) const {
    double thresh = state_.bps_intra_ema;

    // Apply hysteresis if last frame was residual
    if (state_.last_mode == FrameMode::USE_RESIDUAL) {
        thresh -= state_.hysteresis_bpp;
    }

    // Use intra if residual would be too expensive
    return (stats.bps_res + state_.margin_bpp >= thresh);
}

// ============================================================================
// Residual Statistics Computation
// ============================================================================

ResidualStats compute_residual_stats(
    const int16_t* residual,
    size_t pixel_count,
    uint32_t dead_zone_T,
    const int16_t* quantized)
{
    ResidualStats stats;

    if (pixel_count == 0) {
        return stats;
    }

    // Build histogram of |R| for basic statistics
    constexpr size_t NUM_BINS = 1024;
    std::vector<uint64_t> hist_abs(NUM_BINS, 0);
    uint64_t zero_count = 0;
    double sum_abs = 0.0;

    for (size_t i = 0; i < pixel_count; ++i) {
        int32_t mag = std::abs(static_cast<int32_t>(residual[i]));

        // Count samples within dead-zone
        if (mag <= static_cast<int32_t>(dead_zone_T)) {
            zero_count++;
        }

        sum_abs += mag;

        // Accumulate histogram
        if (mag >= static_cast<int32_t>(NUM_BINS)) {
            mag = NUM_BINS - 1;
        }
        hist_abs[mag]++;
    }

    // Zero mass (fraction within dead-zone)
    stats.zero_mass = static_cast<double>(zero_count) / pixel_count;

    // Mean absolute residual
    stats.mean_abs = sum_abs / pixel_count;

    // Percentiles
    uint64_t p95_count = static_cast<uint64_t>(0.95 * pixel_count);
    uint64_t p99_count = static_cast<uint64_t>(0.99 * pixel_count);
    uint64_t cumulative = 0;
    bool found_p95 = false;
    bool found_p99 = false;

    for (size_t i = 0; i < NUM_BINS; ++i) {
        cumulative += hist_abs[i];
        if (!found_p95 && cumulative >= p95_count) {
            stats.p95 = static_cast<double>(i);
            found_p95 = true;
        }
        if (!found_p99 && cumulative >= p99_count) {
            stats.p99 = static_cast<double>(i);
            found_p99 = true;
            break;
        }
    }

    // Entropy of quantized symbols (if provided)
    if (quantized != nullptr) {
        // Build histogram of quantized values
        // Use a map since quantized values can be sparse
        std::unordered_map<int16_t, uint64_t> quant_hist;

        for (size_t i = 0; i < pixel_count; ++i) {
            quant_hist[quantized[i]]++;
        }

        // Compute entropy
        double H = 0.0;
        for (const auto& pair : quant_hist) {
            if (pair.second > 0) {
                double p = static_cast<double>(pair.second) / pixel_count;
                H -= p * std::log2(p);
            }
        }

        stats.bps_res = H;
    } else {
        // Estimate from raw residual histogram (less accurate)
        double H = 0.0;
        for (size_t i = 0; i < NUM_BINS; ++i) {
            if (hist_abs[i] > 0) {
                double p = static_cast<double>(hist_abs[i]) / pixel_count;
                H -= p * std::log2(p);
            }
        }
        // Account for sign bit
        stats.bps_res = H + 1.0;
    }

    return stats;
}

} // namespace lwir
