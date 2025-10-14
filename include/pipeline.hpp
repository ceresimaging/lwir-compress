#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <png.h>
#include "config.hpp"
#include "frame.hpp"
#include "encoder.hpp"

namespace lwir {

/**
 * @brief Compression pipeline orchestrator
 *
 * Manages the complete compression workflow:
 * - Load frames from input directory
 * - Apply decision logic (keyframe vs residual)
 * - Encode with CharLS
 * - Track statistics and performance metrics
 * - Write compressed output
 */
class CompressionPipeline {
public:
    /**
     * @brief Construct pipeline with configuration
     * @param config Compression configuration
     */
    explicit CompressionPipeline(const CompressionConfig& config);

    /**
     * @brief Run compression on all frames in input directory
     * @return true if successful, false otherwise
     */
    bool run();

    /**
     * @brief Print compression summary statistics
     */
    void print_summary() const;

    /**
     * @brief Write statistics to JSON file
     * @param output_path Path to output JSON file
     */
    void write_statistics(const std::string& output_path) const;

private:
    CompressionConfig config_;

    // Statistics
    size_t total_original_bytes_;
    size_t total_compressed_bytes_;
    uint64_t total_encode_time_ms_;
    uint32_t frames_processed_;

    /**
     * @brief Load a single frame from PNG file
     * @param png_path Path to 16-bit grayscale PNG
     * @param frame Output frame structure
     * @return true if successful, false otherwise
     */
    bool load_frame_from_png(const std::string& png_path, Frame& frame);

    /**
     * @brief Write compressed frame to binary file
     * @param frame Compressed frame data
     * @param output_dir Output directory path
     * @return true if successful, false otherwise
     */
    bool write_compressed_frame(const CompressedFrame& frame, const std::string& output_dir);
};

} // namespace lwir
