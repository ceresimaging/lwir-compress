#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include "frame.hpp"
#include "residual.hpp"

namespace lwir {

/**
 * CharLS encoder/decoder wrapper
 * Handles JPEG-LS compression with configurable NEAR parameter
 */
class CharlsEncoder {
public:
    CharlsEncoder();
    ~CharlsEncoder();

    /**
     * Encode a 16-bit grayscale image
     * @param data Raw pixel data
     * @param width Image width
     * @param height Image height
     * @param near_param NEAR parameter (0 = lossless, >0 = near-lossless)
     * @param output Encoded data (output)
     * @return true on success
     */
    bool encode(
        const uint16_t* data,
        uint32_t width,
        uint32_t height,
        uint32_t near_param,
        std::vector<uint8_t>& output
    );

    /**
     * Decode a CharLS-encoded image
     * @param encoded Encoded data
     * @param output Decoded pixel data (output)
     * @param width Image width (output)
     * @param height Image height (output)
     * @return true on success
     */
    bool decode(
        const uint8_t* encoded,
        size_t encoded_size,
        std::vector<uint16_t>& output,
        uint32_t& width,
        uint32_t& height
    );

    /**
     * Get last error message
     */
    const std::string& last_error() const { return last_error_; }

private:
    std::string last_error_;
};

/**
 * High-level frame encoder/decoder
 * Handles keyframes, residuals, and closed-loop state
 */
class FrameEncoder {
public:
    FrameEncoder();

    /**
     * Encode intra frame (keyframe)
     */
    bool encode_intra_frame(
        const Frame& frame,
        uint32_t near_lossless,
        CompressedFrame& output,
        bool enable_12bit_mode = false
    );

    /**
     * Encode residual frame
     */
    bool encode_residual_frame(
        const Frame& frame,
        uint32_t near_lossless,
        const QuantizationParams& quant_params,
        CompressedFrame& output
    );

    /**
     * Encode a frame (keyframe or residual)
     * @param frame Input frame
     * @param is_keyframe Force keyframe encoding
     * @param keyframe_near NEAR parameter for keyframes
     * @param residual_near NEAR parameter for residuals
     * @param quant_params Quantization parameters
     * @param output Compressed frame (output)
     * @param enable_12bit_mode Enable 12-bit range mapping
     * @return true on success
     */
    bool encode_frame(
        const Frame& frame,
        bool is_keyframe,
        uint32_t keyframe_near,
        uint32_t residual_near,
        const QuantizationParams& quant_params,
        CompressedFrame& output,
        bool enable_12bit_mode = false
    );

    /**
     * Decode a compressed frame
     * @param compressed Compressed frame
     * @param output Decoded frame (output)
     * @return true on success
     */
    bool decode_frame(
        const CompressedFrame& compressed,
        Frame& output
    );

    /**
     * Reset encoder state (clears reference frame)
     */
    void reset();

private:
    Frame reference_frame_;  // Previous reconstructed frame
    bool reference_frame_initialized_;
};

} // namespace lwir
