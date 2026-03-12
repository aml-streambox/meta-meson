# GStreamer Amlogic YUV422 10-bit to P010 Converter Plugin

GStreamer plugin that converts Amlogic VDIN 40-bit packed YUV422 10-bit format to P010 (YUV420 10-bit) using multi-threaded ARM NEON.

## Features

- **Format Conversion**: 40-bit packed YUV422 10-bit → P010 (YUV420 10-bit)
- **Performance**: 4K60 (3840x2160 @ 60fps)
- **Implementation**: Multi-threaded ARM NEON (6 small cores)
- **Memory**: DMA-BUF and mmap support for zero-copy
- **Platform**: Amlogic A311D2 and similar SoCs

## Performance

| Resolution | CPU Usage | Power | Status |
|-----------|-----------|-------|--------|
| 4K @ 60fps | ~6 cores @ 50% | Low (small cores only) | ✅ Pass |
| 1080p @ 60fps | ~2 cores @ 30% | Very Low | ✅ Pass |

## Dependencies

- GStreamer 1.16+
- gstreamer-base-1.0
- gstreamer-video-1.0
- gstreamer-allocators-1.0 (for DMA-BUF)
- pthread
- ARM NEON (ARMv8-A)

## Build

```bash
# Configure
meson setup build --prefix=/usr

# Build
ninja -C build

# Install
sudo ninja -C build install
```

## Usage

### Basic Pipeline

```bash
gst-launch-1.0 v4l2src device=/dev/video0 ! \
    video/x-raw,format=AMLOGIC_YUV422_10BIT_PACKED,width=3840,height=2160,framerate=60/1 ! \
    amlv10conv ! \
    video/x-raw,format=P010 ! \
    v4l2h264enc ! \
    fakesink
```

### With DMA-BUF (Zero-Copy)

```bash
gst-launch-1.0 v4l2src io-mode=dmabuf ! \
    video/x-raw,format=AMLOGIC_YUV422_10BIT_PACKED,width=3840,height=2160 ! \
    amlv10conv ! \
    video/x-raw,format=P010 ! \
    v4l2h264enc output-io-mode=dmabuf ! \
    fakesink
```

### Debug

```bash
GST_DEBUG=2 gst-launch-1.0 ...
GST_DEBUG=*amlv10conv*:5 gst-launch-1.0 ...
```

### Performance Monitoring

```bash
# Check FPS
GST_TRACERS="framerate" gst-launch-1.0 ...

# Check CPU usage
top -H -p $(pgrep -d',' gst-launch-1.0)

# Check memory bandwidth
cat /sys/class/devfreq/gpufreq/trans_stat
```

## Architecture

```
┌─────────────────────────────────────────────┐
│           GStreamer Pipeline                 │
│                                              │
│  [v4l2src] ──► [amlv10conv] ──► [encoder]   │
│                (this plugin)                 │
│                                              │
│  Input: 40-bit packed YUV422 10-bit         │
│  Output: P010 (YUV420 10-bit)               │
│                                              │
│  6 threads on small cores (0-5)             │
└─────────────────────────────────────────────┘
```

## Files

- `src/gstamlyuv42210bitconv.c/h` - Main plugin
- `src/yuv422_converter_neon.c/h` - NEON converter core
- `meson.build` - Build configuration

## Testing

### Unit Test

```bash
# Build test
meson setup build -Dtests=enabled
ninja -C build

# Run tests
./build/tests/test_plugin
```

### Benchmark

```bash
# 4K60 benchmark
gst-launch-1.0 videotestsrc num-buffers=600 ! \
    video/x-raw,format=AMLOGIC_YUV422_10BIT_PACKED,width=3840,height=2160,framerate=60/1 ! \
    amlv10conv ! \
    video/x-raw,format=P010 ! \
    fakesink
```

## Troubleshooting

### High CPU Usage
- Check thread affinity: `taskset -pc <pid>`
- Verify using small cores only: cores 0-5

### Frame Drops
- Enable debug: `GST_DEBUG=*amlv10conv*:5`
- Check conversion time: should be <16.67ms

### Build Errors
- Ensure ARM NEON available: `-march=armv8-a`
- Install GStreamer dev packages

## License

LGPL-2.1+ - See LICENSE file

## Author

Amlogic, Inc.