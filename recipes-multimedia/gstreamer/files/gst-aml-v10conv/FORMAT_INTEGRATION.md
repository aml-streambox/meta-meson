# AMLOGIC_YUV422_10BIT_PACKED Format Integration

## Overview

This document describes how to integrate AMLOGIC_YUV422_10BIT_PACKED format support into the system for gst-aml-v10conv GStreamer plugin.

## Format Details

**Name**: AMLOGIC_YUV422_10BIT_PACKED
**FourCC**: 'AMLY' (0x414D4C59)
**Description**: Amlogic VDIN 40-bit packed YUV422 10-bit
**Memory Layout**: 
- 40 bits per 2 pixels (5 bytes per 2 pixels)
- Layout: U(10) + Y0(10) + V(10) + Y1(10) packed in 5 bytes
- Little-endian byte order
- Total: 2.5 bytes per pixel

## Patches Required

### 1. GStreamer v4l2src Plugin (gst1-plugins-good)

**Location**: `/home/anshi/yocto/meta-meson/recipes-multimedia/gstreamer/gst1-plugins-good/`

**Patch**: `0043-add-AMLOGIC_YUV422_10BIT_PACKED-format-support.patch`

**Modified Files**:
- `sys/v4l2/ext/videodev2.h` - Add FourCC definition
- `sys/v4l2/gstv4l2bufferpool.c` - Add DMABUF buffer resize support
- `sys/v4l2/gstv4l2object.c` - Add format registration and conversion

**Application**:
```bash
cd /path/to/gst-plugins-good-1.x.x
patch -p1 < 0043-add-AMLOGIC_YUV422_10BIT_PACKED-format-support.patch
```

### 2. Kernel V4L2 (Direct Changes - No Patch)

**Files to modify**:
- `include/uapi/linux/videodev2.h`
- `drivers/media/v4l2-core/v4l2-ioctl.c`

**videodev2.h Changes**:
```c
/* Add after P010 definition (around line 615) */
#define V4L2_PIX_FMT_AMLOGIC_YUV422_10BIT_PACKED v4l2_fourcc('A', 'M', 'L', 'Y')
```

**v4l2-ioctl.c Changes**:
```c
/* Add in v4l_fill_fmtdesc() function (around line 1305) */
case V4L2_PIX_FMT_AMLOGIC_YUV422_10BIT_PACKED:
    descr = "Amlogic YUV422 10-bit packed";
    break;
```

### 3. VDIN Driver (Direct Changes)

**File**: `drivers/amlogic/media/vdin/vdin_drv.c` (or similar)

Add format support in VIDIOC_S_FMT handler:
```c
if (v4l2_fmt->fmt.pix_mp.pixelformat == V4L2_PIX_FMT_AMLOGIC_YUV422_10BIT_PACKED) {
    /* 40-bit packed = 5 bytes per 2 pixels = 2.5 bytes per pixel */
    v4l2_fmt->fmt.pix_mp.plane_fmt[0].sizeimage = 
        (v4l2_fmt->fmt.pix_mp.width * v4l2_fmt->fmt.pix_mp.height * 5) / 2;
    v4l2_fmt->fmt.pix_mp.plane_fmt[0].bytesperline = 
        (v4l2_fmt->fmt.pix_mp.width * 5) / 2;
}
```

## Build Order

1. **Kernel** - Add format definition and description
2. **GStreamer** - Apply patch and rebuild
3. **gst-aml-v10conv** - Build plugin

## Testing

### Verify Kernel Support
```bash
# Check if format is recognized
v4l2-ctl -d /dev/video0 --list-formats-ext
```

### Test Pipeline
```bash
gst-launch-1.0 v4l2src device=/dev/video0 ! \
    video/x-raw,format=AMLOGIC_YUV422_10BIT_PACKED,width=3840,height=2160 ! \
    amlv10conv ! \
    video/x-raw,format=P010 ! \
    fakesink
```

### With Encoder
```bash
gst-launch-1.0 v4l2src device=/dev/video0 ! \
    video/x-raw,format=AMLOGIC_YUV422_10BIT_PACKED,width=3840,height=2160,framerate=60/1 ! \
    amlv10conv ! \
    video/x-raw,format=P010 ! \
    v4l2h264enc ! \
    fakesink
```

## Files Modified

### GStreamer Patch (Created)
- `/home/anshi/yocto/meta-meson/recipes-multimedia/gstreamer/gst1-plugins-good/0043-add-AMLOGIC_YUV422_10BIT_PACKED-format-support.patch`

### Plugin Files (Updated)
- `/home/anshi/yocto/gst-aml-v10conv/src/gstamlyuv42210bitconv.c`
- `/home/anshi/yocto/gst-aml-v10conv/IMPLEMENTATION_PLAN.md`
- `/home/anshi/yocto/gst-aml-v10conv/README.md`

## Notes

1. The format uses **'AMLY'** FourCC code (AMLogic Y)
2. DMA-BUF zero-copy is supported for minimal memory bandwidth
3. Format is NOT standard - requires both kernel and GStreamer patches
4. Only supports single-plane format (all data in one buffer)
5. Thread affinity set to small cores (0-5) for power efficiency

## Troubleshooting

### Format not recognized
- Check kernel videodev2.h has the FourCC definition
- Verify v4l2-ioctl.c has the format description
- Rebuild kernel modules

### GStreamer error "Could not negotiate format"
- Verify patch applied to gst-plugins-good
- Check `gst-inspect-1.0 v4l2src` shows AMLOGIC_YUV422_10BIT_PACKED
- Rebuild GStreamer

### High CPU usage
- Verify using DMA-BUF (not mmap)
- Check thread affinity on small cores
- Use `--fps 60` to limit frame rate

## References

- P010 patch: `0042-add-P010-10bit-HDR-format-support.patch` (reference)
- VDIN driver: `drivers/amlogic/media/vdin/`
- GStreamer v4l2: `sys/v4l2/` in gst-plugins-good