# Code that runs in renderer processes (linked into the helper app). Portable.
add_library(brouk_renderer STATIC
  renderer_app.cc
  renderer_app.h
)
# The helper processes cannot load the sanitizer runtime (sandbox).
brouk_target_options(brouk_renderer CEF NO_SANITIZE)
target_link_libraries(brouk_renderer PUBLIC libcef_dll_wrapper)
target_include_directories(brouk_renderer SYSTEM PUBLIC "${CEF_ROOT}")
