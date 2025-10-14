/**
 * @file bitdepth.cpp
 * @brief Bit depth reduction implementation
 */

#include "bitdepth.hpp"
#include <algorithm>
#include <cstring>

namespace lwir {

RangeMap compute_range_map(const uint16_t* data, size_t count)
{
    if (count == 0) {
        return RangeMap(0, 0);
    }

    // Find min and max efficiently
    uint16_t min_val = data[0];
    uint16_t max_val = data[0];

    for (size_t i = 1; i < count; ++i) {
        if (data[i] < min_val) min_val = data[i];
        if (data[i] > max_val) max_val = data[i];
    }

    return RangeMap(min_val, max_val);
}

void map_to_12bit(
    const uint16_t* src,
    uint16_t* dst,
    size_t count,
    const RangeMap& map)
{
    // Map [min_value, max_value] → [0, 4095]
    // dst = (src - min) * 4095 / range

    if (map.range == 0) {
        // Constant image, map everything to 0
        std::memset(dst, 0, count * sizeof(uint16_t));
        return;
    }

    const uint32_t min = map.min_value;
    const uint32_t range = map.range;

    // Use 32-bit arithmetic to avoid overflow
    // Add range/2 for rounding
    const uint32_t rounding = range / 2;

    for (size_t i = 0; i < count; ++i) {
        const uint32_t val = src[i] - min;
        const uint32_t mapped = (val * 4095 + rounding) / range;
        dst[i] = static_cast<uint16_t>(mapped);
    }
}

void map_from_12bit(
    const uint16_t* src,
    uint16_t* dst,
    size_t count,
    const RangeMap& map)
{
    // Inverse map: [0, 4095] → [min_value, max_value]
    // dst = src * range / 4095 + min

    if (map.range == 0) {
        // Constant image, map everything to min
        for (size_t i = 0; i < count; ++i) {
            dst[i] = map.min_value;
        }
        return;
    }

    const uint32_t min = map.min_value;
    const uint32_t range = map.range;

    // Add 4095/2 for rounding
    const uint32_t rounding = 2047;  // 4095 / 2

    for (size_t i = 0; i < count; ++i) {
        const uint32_t mapped = (static_cast<uint32_t>(src[i]) * range + rounding) / 4095;
        dst[i] = static_cast<uint16_t>(mapped + min);
    }
}

} // namespace lwir
