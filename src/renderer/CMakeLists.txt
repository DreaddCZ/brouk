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
  JOB_POOL brouk_web
  VERBATIM)
target_sources(brouk_renderer PRIVATE "${_viewer_h}")

# The ad blocker's page scripts (web/adblock, TypeScript), embedded the same
# way (007): cosmetic filtering and the element picker.
set(_adblock_js_dir "${PROJECT_BINARY_DIR}/generated/brouk/adblock")
set(_cosmetic_h "${PROJECT_BINARY_DIR}/generated/brouk/adblock_cosmetic_script.h")
set(_picker_h "${PROJECT_BINARY_DIR}/generated/brouk/adblock_picker_script.h")
add_custom_command(OUTPUT "${_cosmetic_h}" "${_picker_h}"
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/web" adblock "${_adblock_js_dir}"
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_adblock_js_dir}/cosmetic.js -DOUTPUT=${_cosmetic_h}
          -DNAME=kAdblockCosmeticScript -P "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_adblock_js_dir}/picker.js -DOUTPUT=${_picker_h}
          -DNAME=kAdblockPickerScript -P "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
  DEPENDS "${PROJECT_SOURCE_DIR}/web/adblock/cosmetic.ts"
          "${PROJECT_SOURCE_DIR}/web/adblock/picker.ts"
          "${PROJECT_SOURCE_DIR}/web/adblock/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/web/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/web/package-lock.json"
          "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
  COMMENT "Building the ad blocker's page scripts"
  JOB_POOL brouk_web
  VERBATIM)
target_sources(brouk_renderer PRIVATE "${_cosmetic_h}" "${_picker_h}")

# The login forms script (web/forms, TypeScript), embedded the same way (008).
set(_forms_js "${PROJECT_BINARY_DIR}/generated/brouk/forms.js")
set(_forms_h "${PROJECT_BINARY_DIR}/generated/brouk/forms_script.h")
add_custom_command(OUTPUT "${_forms_h}"
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/web" forms "${_forms_js}"
  COMMAND ${CMAKE_COMMAND} -DINPUT=${_forms_js} -DOUTPUT=${_forms_h}
          -DNAME=kFormsScript -P "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
  DEPENDS "${PROJECT_SOURCE_DIR}/web/forms/field_rules.ts"
          "${PROJECT_SOURCE_DIR}/web/forms/address_rules.ts"
          "${PROJECT_SOURCE_DIR}/web/forms/login.ts"
          "${PROJECT_SOURCE_DIR}/web/forms/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/web/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/web/package-lock.json"
          "${PROJECT_SOURCE_DIR}/cmake/EmbedText.cmake"
  COMMENT "Building the login forms script"
  JOB_POOL brouk_web
  VERBATIM)
target_sources(brouk_renderer PRIVATE "${_forms_h}")
