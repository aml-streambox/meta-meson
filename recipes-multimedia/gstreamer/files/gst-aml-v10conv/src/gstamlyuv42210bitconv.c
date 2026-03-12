/* GStreamer Amlogic YUV422 10-bit to P010 Converter
 * 
 * Copyright (C) 2024 Amlogic, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/* Define missing macros for GST_PLUGIN_DEFINE */
#ifndef VERSION
#define VERSION "1.0.0"
#endif

#ifndef PACKAGE
#define PACKAGE "gst-aml-v10conv"
#endif

#ifndef GST_PACKAGE_NAME
#define GST_PACKAGE_NAME "GStreamer Amlogic Plugins"
#endif

#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "https://github.com/amlogic/gst-aml-v10conv"
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamlyuv42210bitconv.h"
#include "yuv422_converter_neon.h"
#include "yuv422_converter_gpu_gles.h"

#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <linux/dma-heap.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include <gst/allocators/gstdmabuf.h>

GST_DEBUG_CATEGORY_STATIC (gst_aml_yuv422_10bit_conv_debug);
#define GST_CAT_DEFAULT gst_aml_yuv422_10bit_conv_debug

/* Properties */
enum
{
  PROP_0,
  PROP_NUM_THREADS,
  PROP_CORE_SELECTION,
  PROP_USE_HW,
  PROP_DUMP_OUTPUT,
  PROP_LAST
};

#define DEFAULT_NUM_THREADS 6
#define DEFAULT_CORE_SELECTION CORE_SMALL_ONLY
#define DEFAULT_USE_HW AML_V10CONV_HW_GPU
#define DEFAULT_DUMP_OUTPUT FALSE

static guint
clamp_threads_for_cores (guint requested, CoreSelection core_selection)
{
  guint n = requested;
  if (n < 1)
    n = 1;
  if (n > 8)
    n = 8;

  switch (core_selection) {
    case CORE_SMALL_ONLY:
      if (n > MAX_THREADS_SMALL)
        n = MAX_THREADS_SMALL;
      break;
    case CORE_BIG_ONLY:
      if (n > MAX_THREADS_BIG)
        n = MAX_THREADS_BIG;
      break;
    case CORE_BOTH:
    default:
      if (n > MAX_THREADS_BOTH)
        n = MAX_THREADS_BOTH;
      break;
  }
  return n;
}

/* Core selection enum for GStreamer */
#define GST_TYPE_CORE_SELECTION (gst_core_selection_get_type ())
static GType
gst_core_selection_get_type (void)
{
  static GType core_selection_type = 0;
  static const GEnumValue core_selections[] = {
    {CORE_SMALL_ONLY, "Small cores only (0-5)", "small"},
    {CORE_BIG_ONLY, "Big cores only (6-7)", "big"},
    {CORE_BOTH, "Both small and big cores", "both"},
    {0, NULL, NULL},
  };

  if (!core_selection_type) {
    core_selection_type = g_enum_register_static ("GstCoreSelection", core_selections);
  }
  return core_selection_type;
}

/* Pad templates */
static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { AMLOGIC_YUV422_10BIT_PACKED, ENCODED }, "
        "width = (int) [ 64, 4096 ], "
        "height = (int) [ 64, 2304 ], "
        "framerate = (fraction) [ 1/1, 60/1 ]")
    );

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { P010_10LE, P010, WAVE521_32BIT }, "
        "width = (int) [ 64, 4096 ], "
        "height = (int) [ 64, 2304 ], "
        "framerate = (fraction) [ 1/1, 60/1 ]")
    );

#define gst_aml_yuv422_10bit_conv_parent_class parent_class
G_DEFINE_TYPE (GstAmlYUV42210BitConv, gst_aml_yuv422_10bit_conv, GST_TYPE_BASE_TRANSFORM);

static void gst_aml_yuv422_10bit_conv_finalize (GObject * object);
static void gst_aml_yuv422_10bit_conv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_aml_yuv422_10bit_conv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static gboolean gst_aml_yuv422_10bit_conv_start (GstBaseTransform * trans);
static gboolean gst_aml_yuv422_10bit_conv_stop (GstBaseTransform * trans);
static gboolean gst_aml_yuv422_10bit_conv_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_aml_yuv422_10bit_conv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_aml_yuv422_10bit_conv_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize);
static gboolean gst_aml_yuv422_10bit_conv_decide_allocation (GstBaseTransform * trans,
    GstQuery * query);
static GstFlowReturn gst_aml_yuv422_10bit_conv_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf);
static GstFlowReturn gst_aml_yuv422_10bit_conv_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_aml_yuv422_10bit_conv_sink_event (GstBaseTransform * trans, GstEvent * event);
static gboolean gst_aml_yuv422_10bit_conv_src_event (GstBaseTransform * trans, GstEvent * event);

static gboolean
ensure_gpu_dump_dmabuf (GstAmlYUV42210BitConv *self, gsize size)
{
  if (self->gpu_dump_fd >= 0 && self->gpu_dump_size >= size && self->gpu_dump_map)
    return TRUE;

  if (self->gpu_dump_map && self->gpu_dump_size > 0) {
    munmap (self->gpu_dump_map, self->gpu_dump_size);
    self->gpu_dump_map = NULL;
    self->gpu_dump_size = 0;
  }
  if (self->gpu_dump_fd >= 0) {
    close (self->gpu_dump_fd);
    self->gpu_dump_fd = -1;
  }

  int heapfd = open ("/dev/dma_heap/system", O_RDWR | O_CLOEXEC);
  if (heapfd < 0)
    heapfd = open ("/dev/dma_heap/system-uncached", O_RDWR | O_CLOEXEC);
  if (heapfd < 0)
    return FALSE;

  struct dma_heap_allocation_data alloc_data;
  memset (&alloc_data, 0, sizeof(alloc_data));
  alloc_data.len = size;
  alloc_data.fd_flags = O_RDWR | O_CLOEXEC;
  alloc_data.heap_flags = 0;

  if (ioctl (heapfd, DMA_HEAP_IOCTL_ALLOC, &alloc_data) < 0) {
    close (heapfd);
    return FALSE;
  }
  close (heapfd);

  void *map = mmap (NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, alloc_data.fd, 0);
  if (map == MAP_FAILED) {
    close (alloc_data.fd);
    return FALSE;
  }

  self->gpu_dump_fd = alloc_data.fd;
  self->gpu_dump_map = map;
  self->gpu_dump_size = size;
  return TRUE;
}

/* Class initialization */
static void
gst_aml_yuv422_10bit_conv_class_init (GstAmlYUV42210BitConvClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->finalize = gst_aml_yuv422_10bit_conv_finalize;
  gobject_class->get_property = gst_aml_yuv422_10bit_conv_get_property;
  gobject_class->set_property = gst_aml_yuv422_10bit_conv_set_property;

  /* Ensure enum type is registered before using it */
  (void) gst_core_selection_get_type ();

  g_object_class_install_property (gobject_class, PROP_NUM_THREADS,
      g_param_spec_uint ("num-threads", "Number of threads",
          "Number of threads to use for conversion (1-8, default 6)",
          1, 8, DEFAULT_NUM_THREADS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CORE_SELECTION,
      g_param_spec_enum ("core-selection", "Core selection",
          "Which CPU cores to use for conversion",
          GST_TYPE_CORE_SELECTION, DEFAULT_CORE_SELECTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_USE_HW,
      g_param_spec_string ("use-hw", "Hardware backend",
          "Select conversion backend: GPU or NEON (default GPU)",
          "GPU", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DUMP_OUTPUT,
      g_param_spec_boolean ("dump-output", "Dump output to sink",
          "If false (default), GPU mode may push dummy output for userspace sinks",
          DEFAULT_DUMP_OUTPUT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Amlogic YUV422 10-bit Converter",
      "Filter/Converter/Video",
      "Converts Amlogic VDIN 40-bit packed YUV422 10-bit to P010 or Wave521 format using ARM NEON",
      "Amlogic <support@amlogic.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  transform_class->start = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_start);
  transform_class->stop = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_stop);
  transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_set_caps);
  transform_class->transform_caps = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_transform_caps);
  transform_class->transform_size = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_transform_size);
  transform_class->decide_allocation = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_decide_allocation);
  transform_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_prepare_output_buffer);
  transform_class->transform = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_transform);
  transform_class->sink_event = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_sink_event);
  transform_class->src_event = GST_DEBUG_FUNCPTR (gst_aml_yuv422_10bit_conv_src_event);

  transform_class->passthrough_on_same_caps = FALSE;
  transform_class->transform_ip_on_passthrough = FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_aml_yuv422_10bit_conv_debug, "amlyuv42210bitconv", 0,
      "Amlogic YUV422 10-bit to P010 Converter");
}

/* Instance initialization */
static void
gst_aml_yuv422_10bit_conv_init (GstAmlYUV42210BitConv * self)
{
  GST_DEBUG_OBJECT (self, "Initializing");

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (self), FALSE);
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (self), FALSE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (self), FALSE);

  self->width = 0;
  self->height = 0;
  self->framerate_n = 0;
  self->framerate_d = 1;
  self->in_size = 0;
  self->out_size = 0;
  self->output_format = OUTPUT_YUV420_P010;  /* Default to P010 */
  self->requested_num_threads = DEFAULT_NUM_THREADS;
  self->num_threads = DEFAULT_NUM_THREADS;
  self->core_selection = DEFAULT_CORE_SELECTION;
  self->use_hw = DEFAULT_USE_HW;
  self->dump_output = DEFAULT_DUMP_OUTPUT;
  self->gpu_dummy_out = NULL;
  self->gpu_dummy_out_size = 0;
  self->gpu_dump_fd = -1;
  self->gpu_dump_map = NULL;
  self->gpu_dump_size = 0;
  self->allocator = NULL;
  
  /* Staging buffer */
  self->staging_mem = NULL;
  self->staging_data = NULL;
  self->staging_mapped = FALSE;
  self->frame_count = 0;
}

/* Finalization */
static void
gst_aml_yuv422_10bit_conv_finalize (GObject * object)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (object);

  GST_DEBUG_OBJECT (self, "Finalizing");

  if (self->allocator) {
    gst_object_unref (self->allocator);
    self->allocator = NULL;
  }

  if (self->gpu_dummy_out) {
    g_free (self->gpu_dummy_out);
    self->gpu_dummy_out = NULL;
    self->gpu_dummy_out_size = 0;
  }

  if (self->gpu_dump_map && self->gpu_dump_size > 0) {
    munmap (self->gpu_dump_map, self->gpu_dump_size);
    self->gpu_dump_map = NULL;
    self->gpu_dump_size = 0;
  }
  if (self->gpu_dump_fd >= 0) {
    close (self->gpu_dump_fd);
    self->gpu_dump_fd = -1;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* Property getter */
static void
gst_aml_yuv422_10bit_conv_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (object);

  switch (prop_id) {
    case PROP_NUM_THREADS:
      g_value_set_uint (value, self->requested_num_threads);
      break;
    case PROP_CORE_SELECTION:
      g_value_set_enum (value, self->core_selection);
      break;
    case PROP_USE_HW:
      g_value_set_string (value, self->use_hw == AML_V10CONV_HW_GPU ? "GPU" : "NEON");
      break;
    case PROP_DUMP_OUTPUT:
      g_value_set_boolean (value, self->dump_output);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Property setter */
static void
gst_aml_yuv422_10bit_conv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (object);

  switch (prop_id) {
    case PROP_NUM_THREADS:
      self->requested_num_threads = g_value_get_uint (value);
      self->num_threads = clamp_threads_for_cores (self->requested_num_threads, self->core_selection);
      if (self->num_threads != self->requested_num_threads) {
        GST_WARNING_OBJECT (self, "Limiting threads to %u for current core-selection", self->num_threads);
      }
      GST_DEBUG_OBJECT (self, "Set num-threads requested=%u effective=%u",
          self->requested_num_threads, self->num_threads);
      break;
    case PROP_CORE_SELECTION:
      self->core_selection = g_value_get_enum (value);
      self->num_threads = clamp_threads_for_cores (self->requested_num_threads, self->core_selection);
      GST_DEBUG_OBJECT (self, "Set core-selection to %d", self->core_selection);
      break;
    case PROP_USE_HW:
    {
      const gchar *v = g_value_get_string (value);
      if (v && g_ascii_strcasecmp (v, "GPU") == 0) {
        self->use_hw = AML_V10CONV_HW_GPU;
      } else if (v && g_ascii_strcasecmp (v, "NEON") == 0) {
        self->use_hw = AML_V10CONV_HW_NEON;
      } else {
        GST_WARNING_OBJECT (self, "Unknown use-hw value '%s', fallback to GPU", v ? v : "(null)");
        self->use_hw = AML_V10CONV_HW_GPU;
      }
      GST_INFO_OBJECT (self, "Set use-hw to %s", self->use_hw == AML_V10CONV_HW_GPU ? "GPU" : "NEON");
      break;
    }
    case PROP_DUMP_OUTPUT:
      self->dump_output = g_value_get_boolean (value);
      GST_INFO_OBJECT (self, "Set dump-output to %s", self->dump_output ? "true" : "false");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  if (GST_STATE (self) >= GST_STATE_READY) {
    yuv422_10bit_thread_pool_configure (self->num_threads, self->core_selection);
  }
}

/* Start processing */
static gboolean
gst_aml_yuv422_10bit_conv_start (GstBaseTransform * trans)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);

  GST_DEBUG_OBJECT (self, "Starting");

  /* Create DMA-BUF allocator if available */
  self->allocator = gst_dmabuf_allocator_new ();
  if (!self->allocator) {
    GST_WARNING_OBJECT (self, "Failed to create DMA-BUF allocator, using system memory");
    self->allocator = gst_allocator_find (NULL);
  }

  if (yuv422_10bit_thread_pool_configure (self->num_threads, self->core_selection) != 0) {
    GST_WARNING_OBJECT (self, "Failed to initialize persistent converter thread pool");
  }

  if (self->use_hw == AML_V10CONV_HW_GPU) {
    if (yuv422_gpu_gles_init () != 0) {
      GST_WARNING_OBJECT (self, "GPU init failed (%s), fallback to NEON",
          yuv422_gpu_gles_last_error ());
      self->use_hw = AML_V10CONV_HW_NEON;
    } else {
      GST_WARNING_OBJECT (self, "GPU backend initialized and selected");
    }
  }

  GST_WARNING_OBJECT (self, "amlv10conv start: backend=%s threads=%u core-selection=%d",
      self->use_hw == AML_V10CONV_HW_GPU ? "GPU" : "NEON",
      self->num_threads, self->core_selection);

  return TRUE;
}

/* Stop processing */
static gboolean
gst_aml_yuv422_10bit_conv_stop (GstBaseTransform * trans)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);

  GST_DEBUG_OBJECT (self, "Stopping");

  /* Unmap and free staging buffer */
  if (self->staging_mapped) {
    gst_memory_unmap (self->staging_mem, &self->staging_map);
    self->staging_mapped = FALSE;
    self->staging_data = NULL;
  }
  if (self->staging_mem) {
    gst_memory_unref (self->staging_mem);
    self->staging_mem = NULL;
  }

  if (self->allocator) {
    gst_object_unref (self->allocator);
    self->allocator = NULL;
  }

  yuv422_10bit_thread_pool_destroy ();
  yuv422_gpu_gles_cleanup ();

  return TRUE;
}

/* Caps negotiation */
static gboolean
gst_aml_yuv422_10bit_conv_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  GstVideoInfo in_info;
  GstStructure *s;
  const gchar *format_str;
  gint width, height, fps_n = 30, fps_d = 1;

  GST_DEBUG_OBJECT (self, "set_caps: in=%" GST_PTR_FORMAT ", out=%" GST_PTR_FORMAT,
      incaps, outcaps);
  GST_WARNING_OBJECT (self, "set_caps called: in=%" GST_PTR_FORMAT " out=%" GST_PTR_FORMAT,
      incaps, outcaps);

  if (!gst_video_info_from_caps (&in_info, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse input caps");
    return FALSE;
  }

  /* Store video info */
  self->width = in_info.width;
  self->height = in_info.height;
  self->framerate_n = in_info.fps_n;
  self->framerate_d = in_info.fps_d;

  /* Manually parse output caps - gst_video_info_from_caps may not know P010 */
  s = gst_caps_get_structure (outcaps, 0);
  
  if (!gst_structure_get_int (s, "width", &width) ||
      !gst_structure_get_int (s, "height", &height)) {
    GST_ERROR_OBJECT (self, "Failed to get width/height from output caps");
    return FALSE;
  }
  
  /* Verify dimensions match */
  if (width != self->width || height != self->height) {
    GST_ERROR_OBJECT (self, "Output dimensions %dx%d don't match input %dx%d",
        width, height, self->width, self->height);
    return FALSE;
  }
  
  gst_structure_get_fraction (s, "framerate", &fps_n, &fps_d);

  /* Detect output format */
  format_str = gst_structure_get_string (s, "format");
  
  if (g_strcmp0 (format_str, "P010") == 0 ||
      g_strcmp0 (format_str, "P010_10LE") == 0) {
    self->output_format = OUTPUT_YUV420_P010;
    /* Output: P010 = 2 bytes per pixel for Y, 4 bytes per 2x2 block for UV */
    self->out_size = (self->width * self->height * 2) + 
                     ((self->width / 2) * (self->height / 2) * 4);
    GST_INFO_OBJECT (self, "Output format: P010");
  } else if (g_strcmp0 (format_str, "WAVE521_32BIT") == 0) {
    self->output_format = OUTPUT_YUV420_WAVE521_32BIT;
    /* Output: Wave521 = 3 samples per 4 bytes for Y, same for UV (4:2:0) */
    /* Y plane: width * height / 3 * 4 bytes */
    /* UV plane: (width/2) * (height/2) / 3 * 4 bytes */
    self->out_size = ((self->width * self->height + 2) / 3) * 4 +
                     (((self->width / 2) * (self->height / 2) + 2) / 3) * 4;
    GST_INFO_OBJECT (self, "Output format: WAVE521_32BIT");
  } else {
    GST_ERROR_OBJECT (self, "Unknown output format: %s", format_str);
    return FALSE;
  }

  /* Calculate buffer sizes */
  /* Input: 40-bit packed = 5 bytes per 2 pixels */
  self->in_size = (self->width * self->height * 5) / 2;

  /* Allocate staging buffer for cached input copy */
  if (self->staging_mem) {
    if (self->staging_mapped) {
      gst_memory_unmap (self->staging_mem, &self->staging_map);
      self->staging_mapped = FALSE;
    }
    gst_memory_unref (self->staging_mem);
    self->staging_mem = NULL;
    self->staging_data = NULL;
  }

  /* Allocate staging buffer using system memory (cached) */
  self->staging_mem = gst_allocator_alloc (NULL, self->in_size, NULL);
  if (!self->staging_mem) {
    GST_ERROR_OBJECT (self, "Failed to allocate staging buffer (%u bytes)", self->in_size);
    return FALSE;
  }

  /* Map staging buffer for read/write access */
  if (!gst_memory_map (self->staging_mem, &self->staging_map, GST_MAP_READWRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map staging buffer");
    gst_memory_unref (self->staging_mem);
    self->staging_mem = NULL;
    return FALSE;
  }
  self->staging_data = self->staging_map.data;
  self->staging_mapped = TRUE;

  GST_INFO_OBJECT (self, "Allocated staging buffer: %u bytes at %p", 
      self->in_size, self->staging_data);
  GST_INFO_OBJECT (self, "Configured for %dx%d @ %d/%d fps",
      self->width, self->height, self->framerate_n, self->framerate_d);
  GST_INFO_OBJECT (self, "Buffer sizes: in=%u, out=%u", self->in_size, self->out_size);
  GST_WARNING_OBJECT (self, "set_caps done: in_size=%u out_size=%u format=%d",
      self->in_size, self->out_size, self->output_format);

  return TRUE;
}

/* Transform caps */
static GstCaps *
gst_aml_yuv422_10bit_conv_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  GstCaps *result;
  guint i, n;

  result = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);

  for (i = 0; i < n; i++) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    GstStructure *out_s;

    if (direction == GST_PAD_SINK) {
      /* Transform sink caps to src caps (P010_10LE) */
      out_s = gst_structure_copy (s);
      gst_structure_set (out_s, "format", G_TYPE_STRING, "P010_10LE", NULL);
      gst_caps_append_structure (result, out_s);

      /* Also keep P010 alias for compatibility */
      out_s = gst_structure_copy (s);
      gst_structure_set (out_s, "format", G_TYPE_STRING, "P010", NULL);
      gst_caps_append_structure (result, out_s);

      /* Also add WAVE521_32BIT */
      out_s = gst_structure_copy (s);
      gst_structure_set (out_s, "format", G_TYPE_STRING, "WAVE521_32BIT", NULL);
      gst_caps_append_structure (result, out_s);
    } else {
      /* Transform src caps to sink caps (AMLOGIC_YUV422_10BIT_PACKED) */
      out_s = gst_structure_copy (s);
      gst_structure_set (out_s, "format", G_TYPE_STRING, "AMLOGIC_YUV422_10BIT_PACKED", NULL);
      gst_caps_append_structure (result, out_s);

      /* Also add ENCODED for compatibility */
      out_s = gst_structure_copy (s);
      gst_structure_set (out_s, "format", G_TYPE_STRING, "ENCODED", NULL);
      gst_caps_append_structure (result, out_s);
    }
  }

  if (filter) {
    GstCaps *intersection = gst_caps_intersect (result, filter);
    gst_caps_unref (result);
    result = intersection;
  }

  GST_DEBUG_OBJECT (self, "transform_caps: direction=%s, result=%" GST_PTR_FORMAT,
      direction == GST_PAD_SINK ? "sink" : "src", result);

  return result;
}

static gboolean
gst_aml_yuv422_10bit_conv_transform_size (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, gsize size, GstCaps * othercaps, gsize * othersize)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  (void)caps;
  (void)size;
  (void)othercaps;

  if (!othersize)
    return FALSE;

  if (self->in_size == 0 || self->out_size == 0)
    return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_size (trans, direction, caps, size, othercaps, othersize);

  if (direction == GST_PAD_SINK) {
    *othersize = self->out_size;
  } else {
    *othersize = self->in_size;
  }

  GST_DEBUG_OBJECT (self, "transform_size: dir=%s -> %" G_GSIZE_FORMAT,
      direction == GST_PAD_SINK ? "sink" : "src", *othersize);
  return TRUE;
}

/* Decide allocation strategy */
static gboolean
gst_aml_yuv422_10bit_conv_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  GstBufferPool *pool = NULL;
  guint size = 0, min = 0, max = 0;

  GST_DEBUG_OBJECT (self, "decide_allocation passthrough");
  GST_WARNING_OBJECT (self, "decide_allocation called (passthrough)");

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans, query))
    return FALSE;

  if (self->out_size == 0)
    return TRUE;

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (size < self->out_size) {
      GST_WARNING_OBJECT (self,
          "Adjust allocation pool size from %u to %u (P010 required)",
          size, self->out_size);
      gst_query_set_nth_allocation_pool (query, 0, pool, self->out_size, min, max);
    }
    if (pool)
      gst_object_unref (pool);
  } else {
    GST_WARNING_OBJECT (self, "No allocation pool from downstream, adding required size=%u", self->out_size);
    gst_query_add_allocation_pool (query, NULL, self->out_size, 2, 8);
  }

  return TRUE;
}

static GstFlowReturn
gst_aml_yuv422_10bit_conv_prepare_output_buffer (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer ** outbuf)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  (void) inbuf;

  *outbuf = gst_buffer_new_allocate (NULL, self->out_size, NULL);
  if (!*outbuf) {
    GST_ERROR_OBJECT (self, "Failed to allocate output buffer size=%u", self->out_size);
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "Allocated output buffer with size=%u", self->out_size);
  return GST_FLOW_OK;
}

/* Transform frame */
static GstFlowReturn
gst_aml_yuv422_10bit_conv_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  GstMapInfo in_map, out_map;
  GstFlowReturn ret = GST_FLOW_OK;
  ConversionParams params;
  guint64 start_time, end_time;
  gboolean gpu_mapped_dump_path = FALSE;

  start_time = gst_util_get_timestamp ();
  self->frame_count++;
  if (self->frame_count == 1) {
    GST_WARNING_OBJECT (self, "first transform callback reached");
  }

  if ((self->frame_count % 30) == 1) {
    GST_WARNING_OBJECT (self, "transform frame=%" G_GUINT64_FORMAT " backend=%s in_size=%u out_size=%u",
        self->frame_count,
        self->use_hw == AML_V10CONV_HW_GPU ? "GPU" : "NEON",
        self->in_size, self->out_size);
  }

  /* GPU mode requires strict DMABUF->DMABUF conversion */
  if (self->use_hw == AML_V10CONV_HW_GPU && self->output_format == OUTPUT_YUV420_P010) {
    GstMemory *in_mem0 = gst_buffer_n_memory(inbuf) > 0 ? gst_buffer_peek_memory(inbuf, 0) : NULL;
    GstMemory *out_mem0 = gst_buffer_n_memory(outbuf) > 0 ? gst_buffer_peek_memory(outbuf, 0) : NULL;
    if (!(in_mem0 && out_mem0 && gst_is_dmabuf_memory(in_mem0) && gst_is_dmabuf_memory(out_mem0))) {
      if (!self->dump_output) {
        if (in_mem0 && gst_is_dmabuf_memory(in_mem0)) {
          int in_fd = gst_dmabuf_memory_get_fd(in_mem0);
          if (in_fd >= 0) {
            if (yuv422_gpu_gles_compute_p010_dmabuf(in_fd, self->width, self->height) != 0) {
              GST_ERROR_OBJECT (self, "GPU dmabuf compute-only failed (%s)",
                  yuv422_gpu_gles_last_error ());
              return GST_FLOW_ERROR;
            }
          } else {
            GST_ERROR_OBJECT (self, "Invalid DMABUF input fd in compute-only path");
            return GST_FLOW_ERROR;
          }
        } else {
          GstMapInfo in_map_gpu;
          ConversionParams gpu_params;
          memset (&gpu_params, 0, sizeof(gpu_params));
          if (!gst_buffer_map (inbuf, &in_map_gpu, GST_MAP_READ)) {
            GST_ERROR_OBJECT (self, "Failed to map input buffer for GPU compute-only path");
            return GST_FLOW_ERROR;
          }
          gpu_params.width = self->width;
          gpu_params.height = self->height;
          gpu_params.input_data = in_map_gpu.data;
          gpu_params.output_format = OUTPUT_YUV420_P010;
          if (yuv422_gpu_gles_convert_p010 (&gpu_params) != 0) {
            gst_buffer_unmap (inbuf, &in_map_gpu);
            GST_ERROR_OBJECT (self, "GPU compute-only conversion failed (%s)",
                yuv422_gpu_gles_last_error ());
            return GST_FLOW_ERROR;
          }
          gst_buffer_unmap (inbuf, &in_map_gpu);
        }

        gsize sz = gst_buffer_get_size(outbuf);
        if (sz > 0) gst_buffer_memset(outbuf, 0, 0, sz);
        end_time = gst_util_get_timestamp ();
        if ((self->frame_count % 30) == 1) {
          guint64 dur_us_dbg = (end_time - start_time) / GST_USECOND;
          gdouble dur_ms_dbg = ((gdouble) (end_time - start_time)) / ((gdouble) GST_MSECOND);
          GST_WARNING_OBJECT (self, "GPU mode userspace sink detected: output is faked (dump-output=false)");
          GST_WARNING_OBJECT (self,
              "frame=%" G_GUINT64_FORMAT " compute-only path in %" G_GUINT64_FORMAT " us (%.3f ms, output faked)",
              self->frame_count, dur_us_dbg, dur_ms_dbg);
        }
        return GST_FLOW_OK;
      }
      if (!(in_mem0 && gst_is_dmabuf_memory(in_mem0))) {
        GST_ERROR_OBJECT (self, "GPU dump-output=true requires DMABUF input");
        return GST_FLOW_ERROR;
      }
      if (!ensure_gpu_dump_dmabuf (self, self->out_size)) {
        GST_ERROR_OBJECT (self, "Failed to allocate internal dump DMABUF size=%u", self->out_size);
        return GST_FLOW_ERROR;
      }

      int in_fd = gst_dmabuf_memory_get_fd(in_mem0);
      if (in_fd < 0) {
        GST_ERROR_OBJECT (self, "Invalid DMABUF input fd for dump-output=true");
        return GST_FLOW_ERROR;
      }
      if (yuv422_gpu_gles_convert_p010_dmabuf(in_fd, self->gpu_dump_fd, self->width, self->height) != 0) {
        GST_ERROR_OBJECT (self, "GPU dmabuf conversion failed (%s)",
            yuv422_gpu_gles_last_error ());
        return GST_FLOW_ERROR;
      }

      if (!gst_buffer_map (outbuf, &out_map, GST_MAP_WRITE)) {
        GST_ERROR_OBJECT (self, "Failed to map output buffer for dump copy");
        return GST_FLOW_ERROR;
      }
      if (out_map.size < self->out_size) {
        GST_ERROR_OBJECT (self, "Output buffer too small for dump copy: %" G_GSIZE_FORMAT " < %u", out_map.size, self->out_size);
        gst_buffer_unmap (outbuf, &out_map);
        return GST_FLOW_ERROR;
      }
      memcpy (out_map.data, self->gpu_dump_map, self->out_size);
      gst_buffer_unmap (outbuf, &out_map);

      end_time = gst_util_get_timestamp ();
      if ((self->frame_count % 30) == 1) {
        guint64 dur_us_dbg = (end_time - start_time) / GST_USECOND;
        gdouble dur_ms_dbg = ((gdouble) (end_time - start_time)) / ((gdouble) GST_MSECOND);
        GST_WARNING_OBJECT (self,
            "frame=%" G_GUINT64_FORMAT " dump-output copy path in %" G_GUINT64_FORMAT " us (%.3f ms)",
            self->frame_count, dur_us_dbg, dur_ms_dbg);
      }
      return GST_FLOW_OK;
    }

    int in_fd = gst_dmabuf_memory_get_fd(in_mem0);
    int out_fd = gst_dmabuf_memory_get_fd(out_mem0);
    if (in_fd < 0 || out_fd < 0) {
      GST_ERROR_OBJECT (self, "Invalid DMABUF fd in GPU mode");
      return GST_FLOW_ERROR;
    }

    if (yuv422_gpu_gles_convert_p010_dmabuf(in_fd, out_fd, self->width, self->height) != 0) {
      GST_ERROR_OBJECT (self, "GPU dmabuf conversion failed (%s)",
          yuv422_gpu_gles_last_error ());
      return GST_FLOW_ERROR;
    }

    end_time = gst_util_get_timestamp ();
    if ((self->frame_count % 30) == 1) {
      guint64 dur_us_dbg = (end_time - start_time) / GST_USECOND;
      gdouble dur_ms_dbg = ((gdouble) (end_time - start_time)) / ((gdouble) GST_MSECOND);
      GST_WARNING_OBJECT (self,
          "frame=%" G_GUINT64_FORMAT " converted in %" G_GUINT64_FORMAT " us (%.3f ms, GPU-dmabuf)",
          self->frame_count, dur_us_dbg, dur_ms_dbg);
    }

    return GST_FLOW_OK;
  }

  /* Map input buffer */
  if (!gst_buffer_map (inbuf, &in_map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  /* Map output buffer */
  if (!gst_buffer_map (outbuf, &out_map, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Failed to map output buffer");
    gst_buffer_unmap (inbuf, &in_map);
    return GST_FLOW_ERROR;
  }

  GST_LOG_OBJECT (self, "Transform: in=%p (%" G_GSIZE_FORMAT " bytes), out=%p (%" G_GSIZE_FORMAT " bytes)",
      in_map.data, in_map.size, out_map.data, out_map.size);

  if (in_map.size < self->in_size || out_map.size < self->out_size) {
    GST_ERROR_OBJECT (self, "Buffer too small: in=%" G_GSIZE_FORMAT "/%u out=%" G_GSIZE_FORMAT "/%u",
        in_map.size, self->in_size, out_map.size, self->out_size);
    ret = GST_FLOW_ERROR;
    goto done;
  }

  /* Setup conversion parameters */
  params.width = self->width;
  params.height = self->height;
  params.input_data = in_map.data;
  params.staging_data = self->staging_data;  /* Cached staging buffer */
  params.output_format = self->output_format;
  params.num_threads = self->num_threads;
  params.core_selection = self->core_selection;

  /* Call appropriate converter based on output format */
  if (self->output_format == OUTPUT_YUV420_P010) {
    params.output_y = (guint16 *) out_map.data;
    params.output_uv = (guint16 *) (out_map.data + self->width * self->height * 2);
    params.output_y_wave521 = NULL;
    params.output_uv_wave521 = NULL;

    if (self->use_hw == AML_V10CONV_HW_GPU) {
      if (!self->dump_output && !gpu_mapped_dump_path) {
        GST_ERROR_OBJECT (self, "GPU mode requires DMABUF path; mapped fallback disabled");
        ret = GST_FLOW_ERROR;
        goto done;
      }
      if (yuv422_gpu_gles_convert_p010 (&params) != 0) {
        GST_ERROR_OBJECT (self, "GPU mapped conversion failed (%s)",
            yuv422_gpu_gles_last_error ());
        ret = GST_FLOW_ERROR;
        goto done;
      }
    } else {
      if (yuv422_10bit_convert_neon (&params) != 0) {
        GST_ERROR_OBJECT (self, "P010 conversion failed");
        ret = GST_FLOW_ERROR;
        goto done;
      }
    }
  } else if (self->output_format == OUTPUT_YUV420_WAVE521_32BIT) {
    params.output_y = NULL;
    params.output_uv = NULL;
    params.output_y_wave521 = (guint32 *) out_map.data;
    /* UV plane starts after Y plane */
    guint y_plane_size = ((self->width * self->height + 2) / 3) * 4;
    params.output_uv_wave521 = (guint32 *) (out_map.data + y_plane_size);

    /* Perform Wave521 conversion using NEON */
    if (yuv422_10bit_convert_wave521 (&params) != 0) {
      GST_ERROR_OBJECT (self, "Wave521 conversion failed");
      ret = GST_FLOW_ERROR;
      goto done;
    }
  } else {
    GST_ERROR_OBJECT (self, "Unknown output format: %d", self->output_format);
    ret = GST_FLOW_ERROR;
    goto done;
  }

  end_time = gst_util_get_timestamp ();
  GST_LOG_OBJECT (self, "Conversion took %" GST_TIME_FORMAT,
      GST_TIME_ARGS (end_time - start_time));

  if ((self->frame_count % 30) == 1) {
    guint64 dur_us_dbg = (end_time - start_time) / GST_USECOND;
    gdouble dur_ms_dbg = ((gdouble) (end_time - start_time)) / ((gdouble) GST_MSECOND);
    GST_WARNING_OBJECT (self,
        "frame=%" G_GUINT64_FORMAT " converted in %" G_GUINT64_FORMAT " us (%.3f ms, %s)",
        self->frame_count, dur_us_dbg, dur_ms_dbg,
        self->use_hw == AML_V10CONV_HW_GPU ? "GPU" : "NEON");
  }

  /* Check timing for 4K60 */
  guint64 duration_ms = (end_time - start_time) / GST_MSECOND;
  if (duration_ms > 16 && ((self->frame_count & 0x1F) == 0)) {
    GST_WARNING_OBJECT (self, "Conversion took %" G_GUINT64_FORMAT " ms, may miss 4K60 target",
        duration_ms);
  }

done:
  gst_buffer_unmap (inbuf, &in_map);
  gst_buffer_unmap (outbuf, &out_map);

  return ret;
}

static gboolean
gst_aml_yuv422_10bit_conv_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  GST_WARNING_OBJECT (self, "sink event: %s", GST_EVENT_TYPE_NAME (event));
  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_aml_yuv422_10bit_conv_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstAmlYUV42210BitConv *self = GST_AML_YUV422_10BIT_CONV (trans);
  GST_WARNING_OBJECT (self, "src event: %s", GST_EVENT_TYPE_NAME (event));
  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
}

/* Plugin initialization */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_aml_yuv422_10bit_conv_debug, "amlyuv42210bitconv", 0,
      "Amlogic YUV422 10-bit Converter (P010 and Wave521 formats)");

  return gst_element_register (plugin, "amlv10conv", GST_RANK_PRIMARY + 1,
      GST_TYPE_AML_YUV422_10BIT_CONV);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    amlv10conv,
    "Amlogic YUV422 10-bit Converter (P010 and Wave521 formats)",
    plugin_init,
    VERSION,
    "LGPL",
    GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN
)
