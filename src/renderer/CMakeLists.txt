# Code that runs in renderer processes (linked into the helper app). Portable.
add_library(brouk_renderer STATIC
  renderer_app.cc
  renderer_app.h
  # Validates the browser's answer to the screen sharing picker. Compiled in
  # directly: brouk_core is built with sanitizers, which helpers cannot load.
  "${PROJECT_SOURCE_DIR}/src/core/screen_share.cc"
  # Which pages get the internal pages' bridge.
  "${PROJECT_SOURCE_DIR}/src/core/internal_pages.cc"
)
# The helper processes cannot load the sanitizer runtime (sandbox).
brouk_target_options(brouk_renderer CEF NO_SANITIZE)
target_link_libraries(brouk_renderer PUBLIC libcef_dll_wrapper)
target_include_directories(brouk_renderer SYSTEM PUBLIC "${CEF_ROOT}")

# The JSON viewer (web/jsonview, TypeScript), compiled and embedded as a
# string the renderer adds to JSON documents (006).
set(_viewer_js "${PROJECT_BINARY_DIR}/generated/brouk/json_viewer.js")
set(_viewer_h "${PROJECT_BINARY_DIR}/generated/brouk/json_viewer_script.h")
add_custom_command(OUTPUT "${_viewer_h}"
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/web" jsonview "${_viewer_js}"
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_viewer_js} -DOUTPUT=${_viewer_h}
          -DNAME=kJsonViewerScript -P "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
  DEPENDS "${PROJECT_SOURCE_DIR}/web/jsonview/viewer.ts"
          "${PROJECT_SOURCE_DIR}/web/jsonview/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/web/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/web/package-lock.json"
          "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
  COMMENT "Building the JSON viewer"
  VERBATIM)
target_sources(brouk_renderer PRIVATE "${_viewer_h}")
target_include_directories(brouk_renderer PRIVATE "${PROJECT_BINARY_DIR}/generated")
