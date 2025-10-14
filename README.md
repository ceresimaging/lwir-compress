# LWIR Compress

Temporal JPEG-LS compression library for LWIR (Long-Wave Infrared) thermal imagery.

## Features

- **5× compression ratio** with near-lossless quality
- **Real-time performance** (72-88 fps on ARM Cortex-A57)
- **Temporal residual coding** with closed-loop encoding
- **GOP-based structure** with keyframes and residual frames
- **12-bit range mapping** for limited dynamic range sensors
- **C++14 compatible** for embedded systems

## Performance

Tested on Flight 21052 dataset (4,701 frames @ 30fps):

| Metric | Value |
|--------|-------|
| Compression Ratio | 5.09× |
| Encoding Throughput | 72-88 fps (4 cores ARM) |
| Keyframe RMS Error | 0.4 DN (effectively lossless) |
| Residual RMS Error | 9 DN (below noise floor) |

## Building

```bash
./build.sh
```

Requirements:
- CMake 3.14+
- C++14 compiler
- yaml-cpp
- libpng

## Usage

### As a Library

```cpp
#include "encoder.hpp"

lwir::FrameEncoder encoder;
lwir::CompressedFrame compressed;

encoder.encode_frame(frame, is_keyframe,
                    keyframe_near, residual_near,
                    quant_params, compressed);
```

### As a Standalone Tool

```bash
./build/lwir_compress_tool \
  --input frames/ \
  --output compressed/ \
  --config config.yaml
```

## Integration

This library can be integrated as a git submodule:

```bash
git submodule add https://github.com/ceresimaging/lwir-compress tools/lwir_compress
```

Then link against `liblwir_compress.a` in your build system.

## Algorithm

Uses temporal residual coding with JPEG-LS:

1. **GOP Structure**: Keyframes every 60 frames with predicted frames between
2. **Residual Computation**: Closed-loop encoding prevents error accumulation
3. **Quantization**: Dead-zone + fractional-step quantizer reduces entropy
4. **JPEG-LS Encoding**: CharLS 3.0 for near-lossless compression

## License

See LICENSE file for details.

## Citation

If you use this library in your research, please cite:

```bibtex
@misc{lwir-compress,
  title={LWIR Compress: Temporal JPEG-LS Compression for Thermal Imagery},
  author={Ceres Imaging},
  year={2025},
  url={https://github.com/ceresimaging/lwir-compress}
}
```