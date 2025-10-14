#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace lwir {

/**
 * Frame encoding mode decision
 */
enum class FrameMode {
    USE_INTRA,      // Encode as keyframe (intra)
    USE_RESIDUAL    // Encode as residual (inter)
};

/**
 * Residual statistics for decision logic
 */
struct ResidualStats {
    double mean;
    double p95;
    double p99;
    double entropy;
    double zero_mass;    // Fraction of zero residuals
    double mean_abs;     // Mean absolute residual
    double bps_res;      // Bits per symbol (estimated rate)

    ResidualStats()
        : mean(0.0), p95(0.0), p99(0.0), entropy(0.0),
          zero_mass(0.0), mean_abs(0.0), bps_res(0.0) {}
};

/**
 * Histogram for residual magnitude statistics
 * Bins: 0..1023 (1 DN per bin)
 */
class ResidualHistogram {
public:
    static constexpr size_t NUM_BINS = 1024;

    ResidualHistogram();

    // Accumulate residual magnitudes
    void accumulate(const int16_t* residuals, size_t count);

    // Clear histogram
    void clear();

    // Get histogram data
    const std::vector<uint64_t>& bins() const { return bins_; }

    // Compute statistics
    double mean() const;
    double stddev() const;
    double percentile(double p) const;  // p in [0, 1]
    double max_value() const;
    double entropy() const;  // Shannon entropy in bits

    uint64_t total_samples() const { return total_samples_; }

private:
    std::vector<uint64_t> bins_;
    uint64_t total_samples_;
};

/**
 * Per-frame statistics
 */
struct FrameStats {
    uint32_t frame_index;
    bool is_keyframe;

    // Residual statistics (before quantization)
    double residual_mean;
    double residual_stddev;
    double residual_p95;
    double residual_p99;
    double residual_max;
    double residual_entropy;

    // Quantized residual statistics
    double quantized_entropy;

    // Compression metrics
    uint32_t original_bytes;
    uint32_t compressed_bytes;
    double compression_ratio;
    double encode_time_ms;

    // Error metrics (reconstruction quality)
    double max_error;
    double mean_error;
    double rmse;

    FrameStats()
        : frame_index(0), is_keyframe(false),
          residual_mean(0), residual_stddev(0),
          residual_p95(0), residual_p99(0), residual_max(0),
          residual_entropy(0), quantized_entropy(0),
          original_bytes(0), compressed_bytes(0),
          compression_ratio(0), encode_time_ms(0),
          max_error(0), mean_error(0), rmse(0) {}

    // Format as CSV row
    std::string to_csv() const;

    // CSV header
    static std::string csv_header();
};

/**
 * Aggregate statistics for entire session
 */
struct SessionStats {
    uint32_t total_frames;
    uint32_t keyframes;
    uint32_t residual_frames;

    uint64_t total_original_bytes;
    uint64_t total_compressed_bytes;
    double overall_compression_ratio;

    double avg_encode_time_ms;
    double avg_residual_mean;
    double avg_max_error;
    double avg_rmse;

    SessionStats()
        : total_frames(0), keyframes(0), residual_frames(0),
          total_original_bytes(0), total_compressed_bytes(0),
          overall_compression_ratio(0),
          avg_encode_time_ms(0), avg_residual_mean(0),
          avg_max_error(0), avg_rmse(0) {}

    // Add frame stats
    void add_frame(const FrameStats& fs);

    // Compute final averages
    void finalize();

    // Export to JSON string
    std::string to_json() const;
};

} // namespace lwir
