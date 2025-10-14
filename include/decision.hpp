#pragma once

#include <cstdint>
#include "stats.hpp"

namespace lwir {

/**
 * Decision state tracking
 */
struct DecisionState {
    double bps_intra_ema;     // Exponential moving average of intra bits-per-pixel
    double ema_alpha;         // EMA weight
    uint32_t gop_period;      // Force keyframe every N frames
    uint32_t gop_max;         // Hard cap on residual run length
    double margin_bpp;        // BPP slack for choosing residual
    double hysteresis_bpp;    // BPP stickiness to prevent flip-flop
    bool enable_probe;        // Enable tile probe for tie-breaking
    double probe_band_bpp;    // BPP range for probe activation
    double probe_margin;      // Probe decision margin
    uint32_t frames_since_key;
    FrameMode last_mode;

    // Heuristic thresholds
    double zero_mass_min;     // Minimum fraction of small changes
    double mean_abs_max;      // Maximum mean |R| before forcing keyframe
    double p95_max;           // P95 threshold
    double p99_max;           // P99 threshold

    DecisionState()
        : bps_intra_ema(2.5),     // Initial guess for LWIR
          ema_alpha(0.2),
          gop_period(60),
          gop_max(120),
          margin_bpp(0.3),
          hysteresis_bpp(0.15),
          enable_probe(false),
          probe_band_bpp(0.15),
          probe_margin(0.1),
          frames_since_key(0),
          last_mode(FrameMode::USE_INTRA),
          zero_mass_min(0.75),
          mean_abs_max(12.0),
          p95_max(30.0),
          p99_max(50.0)
    {}
};

/**
 * Decision engine for residual vs intra encoding
 */
class FrameDecisionEngine {
public:
    FrameDecisionEngine(const DecisionState& initial_state = DecisionState());

    /**
     * Decide frame encoding mode
     * @param stats Residual statistics (pre-quantization)
     * @param frame_index Current frame index
     * @return Encoding mode decision
     */
    FrameMode decide_mode(const ResidualStats& stats, uint32_t frame_index);

    /**
     * Update intra BPP estimate after encoding a keyframe
     * @param keyframe_bytes Size of encoded keyframe in bytes
     * @param width Frame width
     * @param height Frame height
     */
    void update_intra_bpp(size_t keyframe_bytes, uint32_t width, uint32_t height);

    /**
     * Mark that a residual frame was encoded
     */
    void mark_residual();

    /**
     * Get current state (for logging/debugging)
     */
    const DecisionState& get_state() const { return state_; }

    /**
     * Update configuration parameters
     */
    void set_state(const DecisionState& state) { state_ = state; }

private:
    DecisionState state_;

    /**
     * Check periodic keyframe conditions
     */
    bool should_force_periodic(uint32_t frame_index) const;

    /**
     * Check heuristic conditions
     */
    bool should_force_heuristic(const ResidualStats& stats) const;

    /**
     * Check rate-based condition with hysteresis
     */
    bool should_use_intra_rate(const ResidualStats& stats) const;
};

/**
 * Compute residual statistics for decision making
 * @param residual Raw residual (before quantization)
 * @param pixel_count Number of pixels
 * @param dead_zone_T Dead-zone threshold
 * @param quantized Quantized residual (for entropy calculation)
 * @return Statistics for decision engine
 */
ResidualStats compute_residual_stats(
    const int16_t* residual,
    size_t pixel_count,
    uint32_t dead_zone_T,
    const int16_t* quantized = nullptr
);

} // namespace lwir
