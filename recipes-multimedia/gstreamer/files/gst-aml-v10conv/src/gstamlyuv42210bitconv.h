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

#ifndef _GST_AML_YUV422_10BIT_CONV_H_
#define _GST_AML_YUV422_10BIT_CONV_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include "yuv422_converter_neon.h"

G_BEGIN_DECLS

#define GST_TYPE_AML_YUV422_10BIT_CONV \
  (gst_aml_yuv422_10bit_conv_get_type())
#define GST_AML_YUV422_10BIT_CONV(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AML_YUV422_10BIT_CONV, GstAmlYUV42210BitConv))
#define GST_AML_YUV422_10BIT_CONV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AML_YUV422_10BIT_CONV, GstAmlYUV42210BitConvClass))
#define GST_IS_AML_YUV422_10BIT_CONV(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AML_YUV422_10BIT_CONV))
#define GST_IS_AML_YUV422_10BIT_CONV_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AML_YUV422_10BIT_CONV))

#define GST_AML_YUV422_10BIT_CONV_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_AML_YUV422_10BIT_CONV, GstAmlYUV42210BitConvClass))

typedef struct _GstAmlYUV42210BitConv GstAmlYUV42210BitConv;
typedef struct _GstAmlYUV42210BitConvClass GstAmlYUV42210BitConvClass;

typedef enum {
  AML_V10CONV_HW_GPU = 0,
  AML_V10CONV_HW_NEON = 1
} AmlV10ConvUseHw;

struct _GstAmlYUV42210BitConv {
  GstBaseTransform element;

  /* Video parameters */
  gint width;
  gint height;
  gint framerate_n;
  gint framerate_d;
  
  /* Buffer sizes */
  guint in_size;
  guint out_size;
  
  /* Output format */
  OutputFormat output_format;
  
  /* CPU configuration */
  guint requested_num_threads;/* User-requested threads (1-8) */
  guint num_threads;          /* Effective threads after core clamp */
  CoreSelection core_selection; /* Which cores to use (default: CORE_SMALL_ONLY) */
  AmlV10ConvUseHw use_hw;
  gboolean dump_output;
  
  /* Staging buffer for cached input copy */
  GstMemory *staging_mem;     /* DMA memory for staging buffer */
  gpointer staging_data;      /* Mapped staging buffer */
  GstMapInfo staging_map;
  gboolean staging_mapped;

  guint64 frame_count;

  gpointer gpu_dummy_out;
  gsize gpu_dummy_out_size;
  gint gpu_dump_fd;
  gpointer gpu_dump_map;
  gsize gpu_dump_size;
  
  /* Allocator */
  GstAllocator *allocator;
};

struct _GstAmlYUV42210BitConvClass {
  GstBaseTransformClass parent_class;
};

GType gst_aml_yuv422_10bit_conv_get_type (void);

G_END_DECLS

#endif /* _GST_AML_YUV422_10BIT_CONV_H_ */
