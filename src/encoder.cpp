/**
 * @file encoder.cpp
 * @brief CharLS encoder/decoder wrapper with closed-loop support
 *
 * Implements frame encoding with temporal residual compression using CharLS JPEG-LS.
 * Maintains reference frame state for closed-loop encoding when NEAR > 0.
 */

#include "encoder.hpp"
#include "bitdepth.hpp"
#include <charls/charls_jpegls_encoder.h>
#include <charls/charls_jpegls_decoder.h>
#include <charls/public_types.h>
#include <cstring>
#include <stdexcept>
#include <iostream>

namespace lwir {

// CharLS success code
static constexpr int CHARLS_SUCCESS = 0;

// Helper function to encode 16-bit data with CharLS (C API)
static bool encode_charls_16bit(
    const uint16_t* data,
    size_t width,
    size_t height,
    uint32_t near_lossless,
    std::vector<uint8_t>& output,
    uint32_t bits_per_sample = 16)
{
    // Create encoder
    charls_jpegls_encoder* encoder = charls_jpegls_encoder_create();
    if (!encoder) {
        std::cerr << "Failed to create CharLS encoder" << std::endl;
        return false;
    }

    // Set frame info
    charls_frame_info frame_info = {};
    frame_info.width = static_cast<uint32_t>(width);
    frame_info.height = static_cast<uint32_t>(height);
    frame_info.bits_per_sample = bits_per_sample;
    frame_info.component_count = 1;

    charls_jpegls_errc err = charls_jpegls_encoder_set_frame_info(encoder, &frame_info);
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_encoder_destroy(encoder);
        std::cerr << "CharLS set_frame_info failed: " << static_cast<int>(err) << std::endl;
        return false;
    }

    // Set near lossless
    err = charls_jpegls_encoder_set_near_lossless(encoder, near_lossless);
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_encoder_destroy(encoder);
        std::cerr << "CharLS set_near_lossless failed" << std::endl;
        return false;
    }

    // Estimate output size and add safety margin
    size_t estimated_size = 0;
    err = charls_jpegls_encoder_get_estimated_destination_size(encoder, &estimated_size);
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_encoder_destroy(encoder);
        std::cerr << "CharLS get_estimated_destination_size failed" << std::endl;
        return false;
    }

    // Add 10% safety margin to estimated size
    output.resize(estimated_size + (estimated_size / 10) + 1024);

    // Set destination
    err = charls_jpegls_encoder_set_destination_buffer(encoder, output.data(), output.size());
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_encoder_destroy(encoder);
        std::cerr << "CharLS set_destination_buffer failed" << std::endl;
        return false;
    }

    // Encode
    const size_t stride = width * sizeof(uint16_t);
    err = charls_jpegls_encoder_encode_from_buffer(encoder,
        reinterpret_cast<const void*>(data),
        width * height * sizeof(uint16_t),
        stride);

    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_encoder_destroy(encoder);
        std::cerr << "CharLS encode failed: " << static_cast<int>(err) << std::endl;
        return false;
    }

    // Get actual bytes written
    size_t bytes_written = 0;
    err = charls_jpegls_encoder_get_bytes_written(encoder, &bytes_written);
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_encoder_destroy(encoder);
        std::cerr << "CharLS get_bytes_written failed" << std::endl;
        return false;
    }

    output.resize(bytes_written);
    charls_jpegls_encoder_destroy(encoder);

    return true;
}

// Helper function to decode 16-bit data with CharLS (C API)
static bool decode_charls_16bit(
    const uint8_t* compressed_data,
    size_t compressed_size,
    size_t width,
    size_t height,
    std::vector<uint16_t>& output)
{
    // Create decoder
    charls_jpegls_decoder* decoder = charls_jpegls_decoder_create();
    if (!decoder) {
        std::cerr << "Failed to create CharLS decoder" << std::endl;
        return false;
    }

    // Set source
    charls_jpegls_errc err = charls_jpegls_decoder_set_source_buffer(
        decoder, compressed_data, compressed_size);
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_decoder_destroy(decoder);
        std::cerr << "CharLS set_source_buffer failed" << std::endl;
        return false;
    }

    // Read header
    err = charls_jpegls_decoder_read_header(decoder);
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_decoder_destroy(decoder);
        std::cerr << "CharLS read_header failed" << std::endl;
        return false;
    }

    // Get frame info
    charls_frame_info frame_info;
    err = charls_jpegls_decoder_get_frame_info(decoder, &frame_info);
    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_decoder_destroy(decoder);
        std::cerr << "CharLS get_frame_info failed" << std::endl;
        return false;
    }

    // Verify dimensions (allow 12 or 16 bit)
    if (frame_info.width != width || frame_info.height != height ||
        (frame_info.bits_per_sample != 16 && frame_info.bits_per_sample != 12)) {
        charls_jpegls_decoder_destroy(decoder);
        std::cerr << "CharLS frame info mismatch" << std::endl;
        return false;
    }

    // Allocate output
    output.resize(width * height);

    // Decode
    const size_t stride = width * sizeof(uint16_t);
    err = charls_jpegls_decoder_decode_to_buffer(
        decoder,
        reinterpret_cast<void*>(output.data()),
        output.size() * sizeof(uint16_t),
        stride);

    if (static_cast<int>(err) != CHARLS_SUCCESS) {
        charls_jpegls_decoder_destroy(decoder);
        std::cerr << "CharLS decode failed: " << static_cast<int>(err) << std::endl;
        return false;
    }

    charls_jpegls_decoder_destroy(decoder);
    return true;
}


FrameEncoder::FrameEncoder()
    : reference_frame_initialized_(false)
{
}

bool FrameEncoder::encode_intra_frame(
    const Frame& frame,
    uint32_t near_lossless,
    CompressedFrame& output,
    bool enable_12bit_mode)
{
    // Encode original frame directly (keyframe)
    output.width = frame.width;
    output.height = frame.height;
    output.timestamp = frame.timestamp;
    output.frame_index = frame.frame_index;
    output.is_keyframe = true;
    output.near_lossless = near_lossless;

    // Clear quantization params for intra frames
    output.quant_Q = 0.0;
    output.dead_zone_T = 0;
    output.fp_bits = 0;

    const size_t pixel_count = frame.width * frame.height;
    const uint16_t* data_to_encode = frame.data.data();
    std::vector<uint16_t> mapped_data;

    // Apply 12-bit range mapping if enabled
    if (enable_12bit_mode) {
        RangeMap range_map = compute_range_map(frame.data.data(), pixel_count);

        if (range_map.is_beneficial()) {
            mapped_data.resize(pixel_count);
            map_to_12bit(frame.data.data(), mapped_data.data(), pixel_count, range_map);
            data_to_encode = mapped_data.data();

            output.use_range_map = true;
            output.range_min = range_map.min_value;
            output.range_max = range_map.max_value;
        } else {
            output.use_range_map = false;
            output.range_min = 0;
            output.range_max = 65535;
        }
    } else {
        output.use_range_map = false;
        output.range_min = 0;
        output.range_max = 65535;
    }

    // Encode with CharLS (use 12-bit if range mapping is enabled)
    const uint32_t bits_per_sample = output.use_range_map ? 12 : 16;
    if (!encode_charls_16bit(data_to_encode, frame.width, frame.height, near_lossless, output.compressed_data, bits_per_sample)) {
        return false;
    }

    // If this is a keyframe with NEAR=0, decode immediately for reference
    // If NEAR>0, we must decode for closed-loop
    if (near_lossless == 0 || near_lossless > 0) {
        std::vector<uint16_t> decoded;
        if (!decode_charls_16bit(
            output.compressed_data.data(),
            output.compressed_data.size(),
            frame.width, frame.height,
            decoded))
        {
            std::cerr << "Failed to decode keyframe for reference" << std::endl;
            return false;
        }

        // If 12-bit mode was used, inverse map back to 16-bit
        if (output.use_range_map) {
            std::vector<uint16_t> unmapped(pixel_count);
            RangeMap range_map(output.range_min, output.range_max);
            map_from_12bit(decoded.data(), unmapped.data(), pixel_count, range_map);
            decoded = std::move(unmapped);
        }

        // Store as reference frame
        reference_frame_.data = std::move(decoded);
        reference_frame_.width = frame.width;
        reference_frame_.height = frame.height;
        reference_frame_.timestamp = frame.timestamp;
        reference_frame_.frame_index = frame.frame_index;
        reference_frame_initialized_ = true;
    }

    return true;
}

bool FrameEncoder::encode_residual_frame(
    const Frame& frame,
    uint32_t near_lossless,
    const QuantizationParams& quant_params,
    CompressedFrame& output)
{
    if (!reference_frame_initialized_) {
        std::cerr << "Cannot encode residual frame: no reference frame" << std::endl;
        return false;
    }

    if (frame.width != reference_frame_.width || frame.height != reference_frame_.height) {
        std::cerr << "Frame size mismatch with reference" << std::endl;
        return false;
    }

    const size_t pixel_count = frame.width * frame.height;

    // Step 1: Compute temporal residual
    std::vector<int16_t> residual(pixel_count);
    compute_residual(
        frame.data.data(),
        reference_frame_.data.data(),
        residual.data(),
        pixel_count);

    // Step 2: Quantize residual
    std::vector<int16_t> quantized(pixel_count);
    quantize_residual(
        residual.data(),
        quantized.data(),
        pixel_count,
        quant_params);

    // Step 3: Convert to unsigned for CharLS (bias by 32768)
    // CharLS expects unsigned data, so we shift signed int16 to uint16
    std::vector<uint16_t> quantized_unsigned(pixel_count);
    for (size_t i = 0; i < pixel_count; ++i) {
        quantized_unsigned[i] = static_cast<uint16_t>(quantized[i] + 32768);
    }

    // Step 4: Encode quantized residual with CharLS
    output.width = frame.width;
    output.height = frame.height;
    output.timestamp = frame.timestamp;
    output.frame_index = frame.frame_index;
    output.is_keyframe = false;
    output.near_lossless = near_lossless;
    output.quant_Q = quant_params.get_Q();
    output.dead_zone_T = quant_params.dead_zone_T;
    output.fp_bits = quant_params.fp_bits;

    if (!encode_charls_16bit(
        quantized_unsigned.data(),
        frame.width, frame.height,
        near_lossless,
        output.compressed_data))
    {
        return false;
    }

    // Step 5: Closed-loop reconstruction if NEAR > 0
    if (near_lossless > 0) {
        // Decode the compressed quantized residual
        std::vector<uint16_t> decoded_unsigned;
        if (!decode_charls_16bit(
            output.compressed_data.data(),
            output.compressed_data.size(),
            frame.width, frame.height,
            decoded_unsigned))
        {
            std::cerr << "Failed to decode residual for closed-loop" << std::endl;
            return false;
        }

        // Convert back to signed
        std::vector<int16_t> decoded_quantized(pixel_count);
        for (size_t i = 0; i < pixel_count; ++i) {
            decoded_quantized[i] = static_cast<int16_t>(decoded_unsigned[i] - 32768);
        }

        // Dequantize
        std::vector<int16_t> reconstructed_residual(pixel_count);
        dequantize_residual(
            decoded_quantized.data(),
            reconstructed_residual.data(),
            pixel_count,
            quant_params);

        // Add back to reference frame
        std::vector<uint16_t> reconstructed_frame(pixel_count);
        add_residual_to_reference(
            reference_frame_.data.data(),
            reconstructed_residual.data(),
            reconstructed_frame.data(),
            pixel_count);

        // Update reference frame for next iteration
        reference_frame_.data = std::move(reconstructed_frame);
        reference_frame_.timestamp = frame.timestamp;
        reference_frame_.frame_index = frame.frame_index;
    }
    else {
        // NEAR=0: Open-loop, just update reference to current frame
        reference_frame_.data = frame.data;
        reference_frame_.timestamp = frame.timestamp;
        reference_frame_.frame_index = frame.frame_index;
    }

    return true;
}

bool FrameEncoder::encode_frame(
    const Frame& frame,
    bool is_keyframe,
    uint32_t keyframe_near,
    uint32_t residual_near,
    const QuantizationParams& quant_params,
    CompressedFrame& output,
    bool enable_12bit_mode)
{
    if (is_keyframe) {
        return encode_intra_frame(frame, keyframe_near, output, enable_12bit_mode);
    }
    else {
        return encode_residual_frame(frame, residual_near, quant_params, output);
    }
}

bool FrameEncoder::decode_frame(
    const CompressedFrame& compressed,
    Frame& output)
{
    output.width = compressed.width;
    output.height = compressed.height;
    output.timestamp = compressed.timestamp;
    output.frame_index = compressed.frame_index;

    if (compressed.is_keyframe) {
        // Decode intra frame directly
        return decode_charls_16bit(
            compressed.compressed_data.data(),
            compressed.compressed_data.size(),
            compressed.width,
            compressed.height,
            output.data);
    }
    else {
        // Decode residual frame
        if (!reference_frame_initialized_) {
            std::cerr << "Cannot decode residual frame: no reference frame" << std::endl;
            return false;
        }

        const size_t pixel_count = compressed.width * compressed.height;

        // Decode quantized residual
        std::vector<uint16_t> decoded_unsigned;
        if (!decode_charls_16bit(
            compressed.compressed_data.data(),
            compressed.compressed_data.size(),
            compressed.width,
            compressed.height,
            decoded_unsigned))
        {
            return false;
        }

        // Convert back to signed
        std::vector<int16_t> decoded_quantized(pixel_count);
        for (size_t i = 0; i < pixel_count; ++i) {
            decoded_quantized[i] = static_cast<int16_t>(decoded_unsigned[i] - 32768);
        }

        // Dequantize
        QuantizationParams quant_params(
            compressed.dead_zone_T,
            compressed.quant_Q,
            compressed.fp_bits);

        std::vector<int16_t> reconstructed_residual(pixel_count);
        dequantize_residual(
            decoded_quantized.data(),
            reconstructed_residual.data(),
            pixel_count,
            quant_params);

        // Add back to reference frame
        output.data.resize(pixel_count);
        add_residual_to_reference(
            reference_frame_.data.data(),
            reconstructed_residual.data(),
            output.data.data(),
            pixel_count);

        // Update reference for next frame
        reference_frame_.data = output.data;
        reference_frame_.timestamp = compressed.timestamp;
        reference_frame_.frame_index = compressed.frame_index;

        return true;
    }
}

void FrameEncoder::reset()
{
    reference_frame_initialized_ = false;
    reference_frame_.data.clear();
}

} // namespace lwir
