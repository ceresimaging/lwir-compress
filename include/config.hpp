#pragma once

#include <string>
#include <cstdint>
#include <yaml-cpp/yaml.h>
#include "stats.hpp"

namespace lwir {

/**
 * @brief Compression configuration structure
 *
 * Contains all parameters for LWIR temporal compression including
 * GOP settings, quantization parameters, and decision thresholds.
 */
struct CompressionConfig {
    // Input/output paths
    std::string input_dir;
    std::string output_dir;

    // GOP (Group of Pictures) settings
    uint32_t gop_period = 60;  // Keyframe every N frames

    // Compression parameters
    uint32_t keyframe_near = 0;    // NEAR for keyframes (0 = lossless)
    uint32_t residual_near = 10;   // NEAR for residuals
    uint32_t dead_zone_T = 2;      // Dead-zone threshold
    double quant_Q = 2.0;          // Quantization step
    uint32_t fp_bits = 8;          // Fixed-point fractional bits

    // Bit depth optimization
    bool enable_12bit_mode = true;  // Map to 12-bit before compression

    // Decision logic thresholds
    double decision_p95_threshold = 30.0;      // P95 threshold for intra decision
    double decision_p99_threshold = 100.0;     // P99 threshold for intra decision
    double decision_entropy_threshold = 6.0;   // Entropy threshold for intra decision
    double decision_hysteresis_bpp = 0.15;     // Hysteresis to prevent flip-flop

    // Output options
    bool write_residual_histograms = false;  // Write CSV histograms
    bool write_decoded_frames = false;       // Write decoded frames for validation

    /**
     * @brief Load configuration from YAML file
     * @param yaml_path Path to YAML configuration file
     * @param profile_name Optional profile name to load
     * @return true if successful, false otherwise
     */
    bool load_from_yaml(const std::string& yaml_path, const std::string& profile_name = "");

    /**
     * @brief Load configuration from YAML node
     * @param node YAML node containing configuration
     * @return true if successful, false otherwise
     */
    bool load_from_node(const YAML::Node& node);

    /**
     * @brief Validate configuration parameters
     * @return true if valid, false otherwise
     */
    bool validate() const;

    /**
     * @brief Print configuration summary to stdout
     */
    void print() const;
};

/**
 * @brief Frame decision engine
 *
 * Implements three-stage decision logic for keyframe vs residual encoding:
 * 1. Periodic forcing (GOP period)
 * 2. Heuristic-based (P95, P99, entropy thresholds)
 * 3. Rate-based proxy with hysteresis
 */
class FrameDecisionEngine {
public:
    /**
     * @brief Construct decision engine with configuration
     * @param config Compression configuration
     */
    explicit FrameDecisionEngine(const CompressionConfig& config);

    /**
     * @brief Decide encoding mode for current frame
     * @param stats Residual statistics for current frame
     * @param frame_index Current frame index
     * @return FrameMode decision (USE_INTRA or USE_RESIDUAL)
     */
    FrameMode decide_mode(const ResidualStats& stats, uint32_t frame_index);

    /**
     * @brief Update EMA statistics after encoding
     * @param compressed_bytes Size of compressed frame in bytes
     * @param was_keyframe True if frame was encoded as keyframe
     */
    void update_stats(size_t compressed_bytes, bool was_keyframe);

private:
    CompressionConfig config_;
    uint32_t last_keyframe_index_;
    uint32_t frames_since_keyframe_;
    FrameMode last_decision_;

    // EMA tracking for rate-based decision
    double ema_residual_bpp_;
    double ema_keyframe_bpp_;
    bool ema_initialized_;
};

} // namespace lwir
