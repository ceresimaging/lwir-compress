/**
 * @file config.cpp
 * @brief Configuration file parsing and management
 *
 * Handles YAML configuration loading with support for multiple profiles
 * and parameter validation.
 */

#include "config.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace lwir {

// Helper function to safely get YAML value with default
template<typename T>
T get_yaml_value(const YAML::Node& node, const std::string& key, const T& default_value)
{
    if (node[key]) {
        return node[key].as<T>();
    }
    return default_value;
}

bool CompressionConfig::load_from_yaml(const std::string& yaml_path, const std::string& profile_name)
{
    try {
        YAML::Node config_file = YAML::LoadFile(yaml_path);

        // Check for profiles section
        if (config_file["profiles"] && config_file["profiles"][profile_name]) {
            // Load from specific profile
            const YAML::Node profile = config_file["profiles"][profile_name];
            return load_from_node(profile);
        }
        else {
            // Load from root
            return load_from_node(config_file);
        }
    }
    catch (const YAML::Exception& e) {
        std::cerr << "YAML parsing error: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "Configuration error: " << e.what() << std::endl;
        return false;
    }
}

bool CompressionConfig::load_from_node(const YAML::Node& node)
{
    // Required parameters
    if (!node["input_dir"] || !node["output_dir"]) {
        std::cerr << "Configuration must specify input_dir and output_dir" << std::endl;
        return false;
    }

    input_dir = node["input_dir"].as<std::string>();
    output_dir = node["output_dir"].as<std::string>();

    // Optional parameters with defaults
    gop_period = get_yaml_value(node, "gop_period", 60u);
    keyframe_near = get_yaml_value(node, "keyframe_near", 0u);
    residual_near = get_yaml_value(node, "residual_near", 10u);
    dead_zone_T = get_yaml_value(node, "dead_zone_T", 2u);
    quant_Q = get_yaml_value(node, "quant_Q", 2.0);
    fp_bits = get_yaml_value(node, "fp_bits", 8u);

    // Bit depth optimization
    enable_12bit_mode = get_yaml_value(node, "enable_12bit_mode", true);

    // Decision thresholds
    decision_p95_threshold = get_yaml_value(node, "decision_p95_threshold", 30.0);
    decision_p99_threshold = get_yaml_value(node, "decision_p99_threshold", 100.0);
    decision_entropy_threshold = get_yaml_value(node, "decision_entropy_threshold", 6.0);
    decision_hysteresis_bpp = get_yaml_value(node, "decision_hysteresis_bpp", 0.15);

    // Output options
    write_residual_histograms = get_yaml_value(node, "write_residual_histograms", false);
    write_decoded_frames = get_yaml_value(node, "write_decoded_frames", false);

    // Validate parameters
    return validate();
}

bool CompressionConfig::validate() const
{
    if (input_dir.empty() || output_dir.empty()) {
        std::cerr << "Input and output directories must be specified" << std::endl;
        return false;
    }

    if (gop_period == 0) {
        std::cerr << "GOP period must be > 0" << std::endl;
        return false;
    }

    if (quant_Q <= 0.0) {
        std::cerr << "Quantization Q must be > 0" << std::endl;
        return false;
    }

    if (fp_bits > 16) {
        std::cerr << "Fixed-point bits must be <= 16" << std::endl;
        return false;
    }

    if (decision_p95_threshold < 0.0 || decision_p99_threshold < 0.0) {
        std::cerr << "Decision thresholds must be >= 0" << std::endl;
        return false;
    }

    if (decision_entropy_threshold < 0.0) {
        std::cerr << "Entropy threshold must be >= 0" << std::endl;
        return false;
    }

    return true;
}

void CompressionConfig::print() const
{
    std::cout << "Configuration:" << std::endl;
    std::cout << "  Input: " << input_dir << std::endl;
    std::cout << "  Output: " << output_dir << std::endl;
    std::cout << "  GOP Period: " << gop_period << std::endl;
    std::cout << "  Keyframe NEAR: " << keyframe_near << std::endl;
    std::cout << "  Residual NEAR: " << residual_near << std::endl;
    std::cout << "  Quantization Q: " << quant_Q << ", T: " << dead_zone_T << ", fp_bits: " << fp_bits << std::endl;
    std::cout << "  Decision P95 threshold: " << decision_p95_threshold << std::endl;
    std::cout << "  Decision P99 threshold: " << decision_p99_threshold << std::endl;
    std::cout << "  Decision entropy threshold: " << decision_entropy_threshold << std::endl;
    std::cout << "  Decision hysteresis: " << decision_hysteresis_bpp << " bpp" << std::endl;
}


FrameDecisionEngine::FrameDecisionEngine(const CompressionConfig& config)
    : config_(config)
    , last_keyframe_index_(0)
    , frames_since_keyframe_(0)
    , last_decision_(FrameMode::USE_INTRA)
    , ema_residual_bpp_(0.0)
    , ema_keyframe_bpp_(0.0)
    , ema_initialized_(false)
{
}

FrameMode FrameDecisionEngine::decide_mode(const ResidualStats& stats, uint32_t frame_index)
{
    frames_since_keyframe_++;

    // Stage 1: Periodic forcing
    if (frames_since_keyframe_ >= config_.gop_period) {
        frames_since_keyframe_ = 0;
        last_keyframe_index_ = frame_index;
        last_decision_ = FrameMode::USE_INTRA;
        return FrameMode::USE_INTRA;
    }

    // Stage 2: Heuristic-based (P95, P99, entropy thresholds)
    if (stats.p95 > config_.decision_p95_threshold ||
        stats.p99 > config_.decision_p99_threshold ||
        stats.entropy > config_.decision_entropy_threshold)
    {
        frames_since_keyframe_ = 0;
        last_keyframe_index_ = frame_index;
        last_decision_ = FrameMode::USE_INTRA;
        return FrameMode::USE_INTRA;
    }

    // Stage 3: Rate-based proxy (if we have EMA history)
    if (ema_initialized_) {
        // Apply hysteresis based on last decision
        double threshold = ema_keyframe_bpp_;
        if (last_decision_ == FrameMode::USE_INTRA) {
            threshold -= config_.decision_hysteresis_bpp;
        }
        else {
            threshold += config_.decision_hysteresis_bpp;
        }

        if (ema_residual_bpp_ > threshold) {
            frames_since_keyframe_ = 0;
            last_keyframe_index_ = frame_index;
            last_decision_ = FrameMode::USE_INTRA;
            return FrameMode::USE_INTRA;
        }
    }

    // Default: use residual
    last_decision_ = FrameMode::USE_RESIDUAL;
    return FrameMode::USE_RESIDUAL;
}

void FrameDecisionEngine::update_stats(size_t compressed_bytes, bool was_keyframe)
{
    // Compute bits per pixel (assuming 640x512 for now; should be configurable)
    const double bits_per_pixel = (compressed_bytes * 8.0) / (640.0 * 512.0);

    // Update EMA
    constexpr double alpha = 0.1; // EMA smoothing factor

    if (!ema_initialized_) {
        if (was_keyframe) {
            ema_keyframe_bpp_ = bits_per_pixel;
        }
        else {
            ema_residual_bpp_ = bits_per_pixel;
        }
        ema_initialized_ = true;
    }
    else {
        if (was_keyframe) {
            ema_keyframe_bpp_ = alpha * bits_per_pixel + (1.0 - alpha) * ema_keyframe_bpp_;
        }
        else {
            ema_residual_bpp_ = alpha * bits_per_pixel + (1.0 - alpha) * ema_residual_bpp_;
        }
    }
}

} // namespace lwir
