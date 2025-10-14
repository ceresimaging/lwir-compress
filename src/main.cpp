/**
 * @file main.cpp
 * @brief Command-line interface for LWIR compression tool
 *
 * Provides a CLI for compressing LWIR thermal imagery with temporal
 * residual compression and JPEG-LS encoding.
 *
 * Usage:
 *   lwir_compress --config example_config.yaml
 *   lwir_compress --input frames/ --output compressed/ --gop 60
 */

#include "pipeline.hpp"
#include "config.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <csignal>
#include <atomic>

namespace {

std::atomic<bool> g_interrupted(false);

void signal_handler(int signal)
{
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nInterrupt received, stopping..." << std::endl;
        g_interrupted = true;
    }
}

void print_usage(const char* program_name)
{
    std::cout << "LWIR Compression Tool - Temporal Residual + JPEG-LS Encoding" << std::endl;
    std::cout << std::endl;
    std::cout << "Usage:" << std::endl;
    std::cout << "  " << program_name << " --config <yaml_file> [--profile <name>]" << std::endl;
    std::cout << "  " << program_name << " --input <dir> --output <dir> [options]" << std::endl;
    std::cout << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --config <path>        Load configuration from YAML file" << std::endl;
    std::cout << "  --profile <name>       Use specific profile from config file" << std::endl;
    std::cout << "  --input <dir>          Input directory containing PNG frames" << std::endl;
    std::cout << "  --output <dir>         Output directory for compressed frames" << std::endl;
    std::cout << "  --gop <N>              GOP period (frames between keyframes)" << std::endl;
    std::cout << "  --keyframe-near <N>    NEAR parameter for keyframes (0=lossless)" << std::endl;
    std::cout << "  --residual-near <N>    NEAR parameter for residual frames" << std::endl;
    std::cout << "  --quant-q <Q>          Quantization parameter Q" << std::endl;
    std::cout << "  --dead-zone <T>        Dead zone threshold T" << std::endl;
    std::cout << "  --fp-bits <N>          Fixed-point fractional bits" << std::endl;
    std::cout << "  --help                 Show this help message" << std::endl;
    std::cout << std::endl;
    std::cout << "Examples:" << std::endl;
    std::cout << "  " << program_name << " --config example_config.yaml" << std::endl;
    std::cout << "  " << program_name << " --config config.yaml --profile high_quality" << std::endl;
    std::cout << "  " << program_name << " --input frames/ --output compressed/ --gop 60" << std::endl;
    std::cout << std::endl;
}

bool parse_command_line(int argc, char** argv, lwir::CompressionConfig& config, std::string& config_file, std::string& profile)
{
    if (argc < 2) {
        return false;
    }

    bool has_config_file = false;
    bool has_input_output = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            return false;
        }
        else if (arg == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --config requires an argument" << std::endl;
                return false;
            }
            config_file = argv[++i];
            has_config_file = true;
        }
        else if (arg == "--profile") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --profile requires an argument" << std::endl;
                return false;
            }
            profile = argv[++i];
        }
        else if (arg == "--input") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --input requires an argument" << std::endl;
                return false;
            }
            config.input_dir = argv[++i];
            has_input_output = true;
        }
        else if (arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --output requires an argument" << std::endl;
                return false;
            }
            config.output_dir = argv[++i];
            has_input_output = true;
        }
        else if (arg == "--gop") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --gop requires an argument" << std::endl;
                return false;
            }
            config.gop_period = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--keyframe-near") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --keyframe-near requires an argument" << std::endl;
                return false;
            }
            config.keyframe_near = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--residual-near") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --residual-near requires an argument" << std::endl;
                return false;
            }
            config.residual_near = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--quant-q") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --quant-q requires an argument" << std::endl;
                return false;
            }
            config.quant_Q = std::stod(argv[++i]);
        }
        else if (arg == "--dead-zone") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --dead-zone requires an argument" << std::endl;
                return false;
            }
            config.dead_zone_T = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--fp-bits") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --fp-bits requires an argument" << std::endl;
                return false;
            }
            config.fp_bits = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else {
            std::cerr << "Error: Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    // Either config file or input/output must be specified
    if (!has_config_file && !has_input_output) {
        std::cerr << "Error: Must specify either --config or --input/--output" << std::endl;
        return false;
    }

    return true;
}

} // anonymous namespace

int main(int argc, char** argv)
{
    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Parse command line arguments
    lwir::CompressionConfig config;
    std::string config_file;
    std::string profile;

    if (!parse_command_line(argc, argv, config, config_file, profile)) {
        print_usage(argv[0]);
        return 1;
    }

    // Load configuration
    if (!config_file.empty()) {
        std::cout << "Loading configuration from: " << config_file << std::endl;
        if (!profile.empty()) {
            std::cout << "Using profile: " << profile << std::endl;
        }

        if (!config.load_from_yaml(config_file, profile)) {
            std::cerr << "Failed to load configuration" << std::endl;
            return 1;
        }
    }

    // Validate configuration
    if (!config.validate()) {
        std::cerr << "Invalid configuration" << std::endl;
        return 1;
    }

    // Print configuration
    std::cout << std::endl;
    config.print();
    std::cout << std::endl;

    // Create and run pipeline
    lwir::CompressionPipeline pipeline(config);

    try {
        if (!pipeline.run()) {
            std::cerr << "Compression pipeline failed" << std::endl;
            return 1;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Exception during compression: " << e.what() << std::endl;
        return 1;
    }

    if (g_interrupted) {
        std::cout << "Compression interrupted by user" << std::endl;
        return 130; // Standard exit code for SIGINT
    }

    std::cout << std::endl;
    std::cout << "Compression completed successfully!" << std::endl;

    return 0;
}
