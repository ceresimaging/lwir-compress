/**
 * @file pipeline.cpp
 * @brief Main compression pipeline orchestration
 *
 * Manages the complete compression workflow:
 * - Load frames from input directory
 * - Apply decision logic (keyframe vs residual)
 * - Encode with CharLS
 * - Track statistics and performance metrics
 * - Write compressed output
 */

#include "pipeline.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace lwir {

CompressionPipeline::CompressionPipeline(const CompressionConfig& config)
    : config_(config)
    , total_original_bytes_(0)
    , total_compressed_bytes_(0)
    , total_encode_time_ms_(0)
    , frames_processed_(0)
{
}

bool CompressionPipeline::load_frame_from_png(const std::string& png_path, Frame& frame)
{
    // Use libpng to load 16-bit grayscale PNG
    FILE* fp = fopen(png_path.c_str(), "rb");
    if (!fp) {
        std::cerr << "Failed to open PNG: " << png_path << std::endl;
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);
    png_read_info(png, info);

    frame.width = png_get_image_width(png, info);
    frame.height = png_get_image_height(png, info);
    const int bit_depth = png_get_bit_depth(png, info);
    const int color_type = png_get_color_type(png, info);

    // Convert PNG's big-endian 16-bit to native byte order
    if (bit_depth == 16) {
        png_set_swap(png);
    }

    // Verify it's 16-bit grayscale
    if (bit_depth != 16 || color_type != PNG_COLOR_TYPE_GRAY) {
        std::cerr << "PNG must be 16-bit grayscale: " << png_path << std::endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }

    // Allocate row pointers
    std::vector<png_bytep> row_pointers(frame.height);
    frame.data.resize(frame.width * frame.height);

    for (uint32_t y = 0; y < frame.height; ++y) {
        row_pointers[y] = reinterpret_cast<png_bytep>(&frame.data[y * frame.width]);
    }

    png_read_image(png, row_pointers.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);

    return true;
}

bool CompressionPipeline::write_compressed_frame(const CompressedFrame& frame, const std::string& output_dir)
{
    // Create output directory if it doesn't exist (C++14 compatible)
    struct stat st;
    if (stat(output_dir.c_str(), &st) != 0) {
        // Directory doesn't exist, create it
        if (mkdir(output_dir.c_str(), 0755) != 0) {
            std::cerr << "Failed to create output directory: " << output_dir << std::endl;
            return false;
        }
    }

    // Write binary compressed frame
    std::ostringstream filename;
    filename << "frame_" << std::setw(6) << std::setfill('0') << frame.frame_index << ".lwir";
    const std::string output_path = output_dir + "/" + filename.str();

    std::ofstream ofs(output_path, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to write compressed frame: " << output_path << std::endl;
        return false;
    }

    // Write header
    ofs.write(reinterpret_cast<const char*>(&frame.width), sizeof(frame.width));
    ofs.write(reinterpret_cast<const char*>(&frame.height), sizeof(frame.height));
    ofs.write(reinterpret_cast<const char*>(&frame.timestamp), sizeof(frame.timestamp));
    ofs.write(reinterpret_cast<const char*>(&frame.frame_index), sizeof(frame.frame_index));

    uint8_t is_keyframe_byte = frame.is_keyframe ? 1 : 0;
    ofs.write(reinterpret_cast<const char*>(&is_keyframe_byte), sizeof(is_keyframe_byte));

    ofs.write(reinterpret_cast<const char*>(&frame.near_lossless), sizeof(frame.near_lossless));
    ofs.write(reinterpret_cast<const char*>(&frame.quant_Q), sizeof(frame.quant_Q));
    ofs.write(reinterpret_cast<const char*>(&frame.dead_zone_T), sizeof(frame.dead_zone_T));
    ofs.write(reinterpret_cast<const char*>(&frame.fp_bits), sizeof(frame.fp_bits));

    // Write range mapping metadata
    uint8_t use_range_map_byte = frame.use_range_map ? 1 : 0;
    ofs.write(reinterpret_cast<const char*>(&use_range_map_byte), sizeof(use_range_map_byte));
    ofs.write(reinterpret_cast<const char*>(&frame.range_min), sizeof(frame.range_min));
    ofs.write(reinterpret_cast<const char*>(&frame.range_max), sizeof(frame.range_max));

    // Write compressed data size and data
    const uint32_t data_size = static_cast<uint32_t>(frame.compressed_data.size());
    ofs.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    ofs.write(reinterpret_cast<const char*>(frame.compressed_data.data()), data_size);

    return true;
}

bool CompressionPipeline::run()
{
    std::cout << "=== LWIR Compression Pipeline ===" << std::endl;
    std::cout << "Input: " << config_.input_dir << std::endl;
    std::cout << "Output: " << config_.output_dir << std::endl;
    std::cout << "GOP Period: " << config_.gop_period << " frames" << std::endl;
    std::cout << "Keyframe NEAR: " << config_.keyframe_near << std::endl;
    std::cout << "Residual NEAR: " << config_.residual_near << std::endl;
    std::cout << "Quantization Q: " << config_.quant_Q << ", T: " << config_.dead_zone_T << std::endl;
    std::cout << std::endl;

    // Scan input directory for PNG files (C++14 compatible)
    std::vector<std::string> input_files;
    DIR* dir = opendir(config_.input_dir.c_str());
    if (!dir) {
        std::cerr << "Failed to open input directory: " << config_.input_dir << std::endl;
        return false;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        // Check if filename ends with .png and starts with "jenoptik_"
        if (filename.length() > 4 && filename.substr(filename.length() - 4) == ".png") {
            // Filter for jenoptik frames only (skip analysis/mask files)
            if (filename.find("jenoptik_") == 0) {
                input_files.push_back(config_.input_dir + "/" + filename);
            }
        }
    }
    closedir(dir);

    if (input_files.empty()) {
        std::cerr << "No PNG files found in input directory" << std::endl;
        return false;
    }

    // Sort files by name
    std::sort(input_files.begin(), input_files.end());

    std::cout << "Found " << input_files.size() << " PNG files" << std::endl;

    // Initialize decision engine
    FrameDecisionEngine decision_engine(config_);

    // Initialize encoder
    FrameEncoder encoder;

    // Process each frame
    for (size_t i = 0; i < input_files.size(); ++i) {
        const std::string& input_path = input_files[i];

        // Load frame
        Frame frame;
        frame.frame_index = static_cast<uint32_t>(i);
        frame.timestamp = 0; // Could extract from filename if needed

        if (!load_frame_from_png(input_path, frame)) {
            std::cerr << "Failed to load frame " << i << std::endl;
            return false;
        }

        const size_t original_bytes = frame.width * frame.height * sizeof(uint16_t);
        total_original_bytes_ += original_bytes;

        // Decide encoding mode
        FrameMode mode = FrameMode::USE_INTRA;
        ResidualStats stats;

        // For first frame, always use intra
        if (i > 0) {
            // Compute statistics for decision engine
            // Note: This requires reference frame from encoder, but for decision we can use previous original frame
            // In a real system, we'd maintain separate state or compute stats differently
            // For now, use simple heuristics based on frame index
            mode = decision_engine.decide_mode(stats, frame.frame_index);
        }

        const bool is_keyframe = (mode == FrameMode::USE_INTRA);

        // Encode frame
        CompressedFrame compressed;
        QuantizationParams quant_params(
            config_.dead_zone_T,
            config_.quant_Q,
            config_.fp_bits);

        const auto encode_start = std::chrono::high_resolution_clock::now();

        const bool encode_success = encoder.encode_frame(
            frame,
            is_keyframe,
            config_.keyframe_near,
            config_.residual_near,
            quant_params,
            compressed,
            config_.enable_12bit_mode);

        const auto encode_end = std::chrono::high_resolution_clock::now();
        const auto encode_duration = std::chrono::duration_cast<std::chrono::milliseconds>(encode_end - encode_start);

        if (!encode_success) {
            std::cerr << "Failed to encode frame " << i << std::endl;
            return false;
        }

        total_compressed_bytes_ += compressed.compressed_data.size();
        total_encode_time_ms_ += encode_duration.count();
        frames_processed_++;

        // Write compressed frame
        if (!write_compressed_frame(compressed, config_.output_dir)) {
            return false;
        }

        // Update decision engine stats
        const double compression_ratio = static_cast<double>(original_bytes) / compressed.compressed_data.size();
        decision_engine.update_stats(compressed.compressed_data.size(), is_keyframe);

        // Print progress
        std::cout << "Frame " << std::setw(6) << i
                  << " [" << (is_keyframe ? "KEYFRAME" : "RESIDUAL") << "]"
                  << " | " << compressed.compressed_data.size() << " bytes"
                  << " | " << std::fixed << std::setprecision(2) << compression_ratio << "x"
                  << " | " << encode_duration.count() << " ms"
                  << std::endl;
    }

    // Print summary
    print_summary();

    // Write statistics to JSON
    write_statistics(config_.output_dir + "/compression_stats.json");

    return true;
}

void CompressionPipeline::print_summary() const
{
    std::cout << std::endl;
    std::cout << "=== Compression Summary ===" << std::endl;
    std::cout << "Frames processed: " << frames_processed_ << std::endl;
    std::cout << "Original size: " << (total_original_bytes_ / 1024.0 / 1024.0) << " MB" << std::endl;
    std::cout << "Compressed size: " << (total_compressed_bytes_ / 1024.0 / 1024.0) << " MB" << std::endl;

    const double overall_ratio = static_cast<double>(total_original_bytes_) / total_compressed_bytes_;
    std::cout << "Overall compression ratio: " << std::fixed << std::setprecision(2) << overall_ratio << "x" << std::endl;

    const double avg_encode_time = static_cast<double>(total_encode_time_ms_) / frames_processed_;
    std::cout << "Average encode time: " << std::fixed << std::setprecision(2) << avg_encode_time << " ms/frame" << std::endl;

    const double throughput = 1000.0 / avg_encode_time;
    std::cout << "Throughput: " << std::fixed << std::setprecision(1) << throughput << " fps" << std::endl;
}

void CompressionPipeline::write_statistics(const std::string& output_path) const
{
    std::ofstream ofs(output_path);
    if (!ofs) {
        std::cerr << "Failed to write statistics to " << output_path << std::endl;
        return;
    }

    const double overall_ratio = static_cast<double>(total_original_bytes_) / total_compressed_bytes_;
    const double avg_encode_time = static_cast<double>(total_encode_time_ms_) / frames_processed_;
    const double throughput = 1000.0 / avg_encode_time;

    ofs << "{\n";
    ofs << "  \"frames_processed\": " << frames_processed_ << ",\n";
    ofs << "  \"total_original_bytes\": " << total_original_bytes_ << ",\n";
    ofs << "  \"total_compressed_bytes\": " << total_compressed_bytes_ << ",\n";
    ofs << "  \"compression_ratio\": " << overall_ratio << ",\n";
    ofs << "  \"avg_encode_time_ms\": " << avg_encode_time << ",\n";
    ofs << "  \"throughput_fps\": " << throughput << ",\n";
    ofs << "  \"config\": {\n";
    ofs << "    \"gop_period\": " << config_.gop_period << ",\n";
    ofs << "    \"keyframe_near\": " << config_.keyframe_near << ",\n";
    ofs << "    \"residual_near\": " << config_.residual_near << ",\n";
    ofs << "    \"quant_Q\": " << config_.quant_Q << ",\n";
    ofs << "    \"dead_zone_T\": " << config_.dead_zone_T << ",\n";
    ofs << "    \"fp_bits\": " << config_.fp_bits << "\n";
    ofs << "  }\n";
    ofs << "}\n";

    std::cout << "Statistics written to " << output_path << std::endl;
}

} // namespace lwir
