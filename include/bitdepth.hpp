#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace lwir {

/**
 * @file bitdepth.hpp
 * @brief Bit depth reduction via range mapping
 *
 * Many LWIR sensors have limited dynamic range (e.g., 10-bit sensor data
 * stored in 16-bit format). This wastes bits and hurts compression.
 *
 * Solution: Map the actual value range to 12 bits before compression,
 * then inverse-map on decompression.
 *
 * Example: If data spans [29134, 34436] (5302 values),
 * map to [0, 4095] (12-bit) for 1.33× additional compression.
 */

/**
 * Range mapping parameters
 */
struct RangeMap {
    uint16_t min_value;  ///< Minimum value in original range
    uint16_t max_value;  ///< Maximum value in original range
    uint32_t range;      ///< max_value - min_value

    RangeMap() : min_value(0), max_value(65535), range(65535) {}

    RangeMap(uint16_t min_val, uint16_t max_val)
        : min_value(min_val)
        , max_value(max_val)
        , range(max_val - min_val)
    {}

    /**
     * Check if range mapping would be beneficial
     * @return true if range is significantly smaller than 16-bit
     */
    bool is_beneficial() const {
        // Only use if range is < 50% of 16-bit (saves at least 1 bit)
        return range < 32768;
    }

    /**
     * Get bits needed to represent the range
     */
    uint32_t bits_needed() const {
        if (range == 0) return 1;
        uint32_t bits = 0;
        uint32_t val = range;
        while (val > 0) {
            bits++;
            val >>= 1;
        }
        return bits;
    }
};

/**
 * Compute range mapping for a frame
 * @param data Frame data
 * @param count Number of pixels
 * @return Range map
 */
RangeMap compute_range_map(const uint16_t* data, size_t count);

/**
 * Map 16-bit data to reduced bit depth (12-bit target)
 * @param src Source 16-bit data
 * @param dst Destination 12-bit data (stored as uint16_t)
 * @param count Number of pixels
 * @param map Range mapping parameters
 */
void map_to_12bit(
    const uint16_t* src,
    uint16_t* dst,
    size_t count,
    const RangeMap& map);

/**
 * Inverse map 12-bit data back to 16-bit range
 * @param src Source 12-bit data (stored as uint16_t)
 * @param dst Destination 16-bit data
 * @param count Number of pixels
 * @param map Range mapping parameters
 */
void map_from_12bit(
    const uint16_t* src,
    uint16_t* dst,
    size_t count,
    const RangeMap& map);

} // namespace lwir
