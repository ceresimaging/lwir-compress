#include "stats.hpp"
#include <cmath>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace lwir {

// ============================================================================
// ResidualHistogram
// ============================================================================

ResidualHistogram::ResidualHistogram()
    : bins_(NUM_BINS, 0), total_samples_(0)
{
}

void ResidualHistogram::accumulate(const int16_t* residuals, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        // Compute magnitude
        int32_t mag = std::abs(static_cast<int32_t>(residuals[i]));

        // Clamp to valid bin range [0, 1023]
        if (mag >= static_cast<int32_t>(NUM_BINS)) {
            mag = NUM_BINS - 1;
        }

        bins_[mag]++;
    }
    total_samples_ += count;
}

void ResidualHistogram::clear() {
    std::fill(bins_.begin(), bins_.end(), 0);
    total_samples_ = 0;
}

double ResidualHistogram::mean() const {
    if (total_samples_ == 0) return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < NUM_BINS; ++i) {
        sum += static_cast<double>(i) * bins_[i];
    }
    return sum / total_samples_;
}

double ResidualHistogram::stddev() const {
    if (total_samples_ == 0) return 0.0;

    double m = mean();
    double sum_sq = 0.0;

    for (size_t i = 0; i < NUM_BINS; ++i) {
        double diff = static_cast<double>(i) - m;
        sum_sq += diff * diff * bins_[i];
    }

    return std::sqrt(sum_sq / total_samples_);
}

double ResidualHistogram::percentile(double p) const {
    if (total_samples_ == 0 || p < 0.0 || p > 1.0) return 0.0;

    uint64_t target_count = static_cast<uint64_t>(p * total_samples_);
    uint64_t cumulative = 0;

    for (size_t i = 0; i < NUM_BINS; ++i) {
        cumulative += bins_[i];
        if (cumulative >= target_count) {
            return static_cast<double>(i);
        }
    }

    return static_cast<double>(NUM_BINS - 1);
}

double ResidualHistogram::max_value() const {
    for (int i = NUM_BINS - 1; i >= 0; --i) {
        if (bins_[i] > 0) {
            return static_cast<double>(i);
        }
    }
    return 0.0;
}

double ResidualHistogram::entropy() const {
    if (total_samples_ == 0) return 0.0;

    double H = 0.0;
    for (size_t i = 0; i < NUM_BINS; ++i) {
        if (bins_[i] > 0) {
            double p = static_cast<double>(bins_[i]) / total_samples_;
            H -= p * std::log2(p);
        }
    }

    return H;
}

// ============================================================================
// FrameStats
// ============================================================================

std::string FrameStats::csv_header() {
    return "frame_index,is_keyframe,"
           "residual_mean,residual_stddev,residual_p95,residual_p99,residual_max,residual_entropy,"
           "quantized_entropy,"
           "original_bytes,compressed_bytes,compression_ratio,"
           "encode_time_ms,"
           "max_error,mean_error,rmse";
}

std::string FrameStats::to_csv() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);

    oss << frame_index << ","
        << (is_keyframe ? "1" : "0") << ","
        << residual_mean << ","
        << residual_stddev << ","
        << residual_p95 << ","
        << residual_p99 << ","
        << residual_max << ","
        << residual_entropy << ","
        << quantized_entropy << ","
        << original_bytes << ","
        << compressed_bytes << ","
        << compression_ratio << ","
        << encode_time_ms << ","
        << max_error << ","
        << mean_error << ","
        << rmse;

    return oss.str();
}

// ============================================================================
// SessionStats
// ============================================================================

void SessionStats::add_frame(const FrameStats& fs) {
    total_frames++;

    if (fs.is_keyframe) {
        keyframes++;
    } else {
        residual_frames++;
    }

    total_original_bytes += fs.original_bytes;
    total_compressed_bytes += fs.compressed_bytes;

    // Accumulate for averages
    avg_encode_time_ms += fs.encode_time_ms;
    avg_residual_mean += fs.residual_mean;
    avg_max_error += fs.max_error;
    avg_rmse += fs.rmse;
}

void SessionStats::finalize() {
    if (total_frames > 0) {
        avg_encode_time_ms /= total_frames;
        avg_residual_mean /= total_frames;
        avg_max_error /= total_frames;
        avg_rmse /= total_frames;
    }

    if (total_original_bytes > 0) {
        overall_compression_ratio = static_cast<double>(total_compressed_bytes) /
                                   static_cast<double>(total_original_bytes);
    }
}

std::string SessionStats::to_json() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);

    oss << "{\n";
    oss << "  \"total_frames\": " << total_frames << ",\n";
    oss << "  \"keyframes\": " << keyframes << ",\n";
    oss << "  \"residual_frames\": " << residual_frames << ",\n";
    oss << "  \"total_original_bytes\": " << total_original_bytes << ",\n";
    oss << "  \"total_compressed_bytes\": " << total_compressed_bytes << ",\n";
    oss << "  \"overall_compression_ratio\": " << overall_compression_ratio << ",\n";
    oss << "  \"avg_encode_time_ms\": " << avg_encode_time_ms << ",\n";
    oss << "  \"avg_residual_mean\": " << avg_residual_mean << ",\n";
    oss << "  \"avg_max_error\": " << avg_max_error << ",\n";
    oss << "  \"avg_rmse\": " << avg_rmse << ",\n";
    oss << "  \"avg_size_per_frame_kb\": "
        << (total_compressed_bytes / 1024.0) / total_frames << "\n";
    oss << "}";

    return oss.str();
}

} // namespace lwir
