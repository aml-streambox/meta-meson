project('gst-aml-v10conv', 'c',
  version : '1.0.0',
  license : 'LGPL-2.1+',
  meson_version : '>= 0.50.0',
  default_options : [ 'warning_level=2',
                      'buildtype=release',
                      'c_std=gnu99' ])

# Dependencies
gst_req = '>= 1.16.0'
gst_dep = dependency('gstreamer-1.0', version : gst_req)
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req)
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req)
gstalloc_dep = dependency('gstreamer-allocators-1.0', version : gst_req)
thread_dep = dependency('threads')
wayland_client_dep = dependency('wayland-client')
wayland_egl_dep = dependency('wayland-egl')
egl_dep = dependency('egl')
glesv2_dep = dependency('glesv2')

# ARM NEON flags
neon_flags = ['-march=armv8-a', '-O3', '-D_GNU_SOURCE']

# Source files
sources = files(
  'src/gstamlyuv42210bitconv.c',
  'src/yuv422_converter_neon.c',
  'src/yuv422_converter_gpu_gles.c'
)

# Headers
inc = include_directories('src')

# Build plugin
plugin = shared_library('gstamlv10conv',
  sources,
  c_args : neon_flags,
  include_directories : inc,
  dependencies : [gst_dep, gstbase_dep, gstvideo_dep, gstalloc_dep, thread_dep,
                  wayland_client_dep, wayland_egl_dep, egl_dep, glesv2_dep],
  install : true,
  install_dir : get_option('libdir') / 'gstreamer-1.0'
)

# Install headers
install_headers(
  'src/gstamlyuv42210bitconv.h',
  'src/yuv422_converter_neon.h',
  'src/yuv422_converter_gpu_gles.h',
  subdir : 'gst-aml-v10conv'
)
