#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace lwir {

/**
 * Represents a single LWIR frame with metadata
 */
struct Frame {
    std::vector<uint16_t> data;  // 16-bit grayscale pixel data
    uint32_t width;
    uint32_t height;
    uint64_t timestamp;          // microseconds or frame number
    uint32_t frame_index;

    Frame() : width(0), height(0), timestamp(0), frame_index(0) {}

    Frame(uint32_t w, uint32_t h, uint32_t idx = 0, uint64_t ts = 0)
        : data(w * h, 0), width(w), height(h), timestamp(ts), frame_index(idx) {}

    size_t pixel_count() const { return width * height; }

    bool is_valid() const {
        return !data.empty() && width > 0 && height > 0
               && data.size() == pixel_count();
    }
};

/**
 * Compressed frame data with metadata
 */
struct CompressedFrame {
    std::vector<uint8_t> compressed_data;  // CharLS-encoded data
    uint32_t width;
    uint32_t height;
    uint32_t frame_index;
    uint64_t timestamp;
    bool is_keyframe;

    // Compression parameters used
    uint32_t near_lossless;      // NEAR parameter for CharLS
    double quant_Q;              // Quantization step (original value)
    uint32_t dead_zone_T;        // Dead-zone threshold
    uint32_t fp_bits;            // Fractional bits for fixed-point Q

    // Range mapping for bit depth reduction (12-bit optimization)
    uint16_t range_min;          // Minimum value in original range
    uint16_t range_max;          // Maximum value in original range
    bool use_range_map;          // Whether range mapping was used

    CompressedFrame()
        : width(0), height(0), frame_index(0), timestamp(0), is_keyframe(false),
          near_lossless(0), quant_Q(0.0), dead_zone_T(0), fp_bits(0),
          range_min(0), range_max(65535), use_range_map(false) {}
};

} // namespace lwir
