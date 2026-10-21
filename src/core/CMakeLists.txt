# Portable core: no CEF, no platform headers. Builds and is tested on Linux.
set(BROUK_CORE_SOURCES
  address_input.cc
  address_input.h
  context_menu.cc
  context_menu.h
  crash_error_code.cc
  crash_error_code.h
  dialog_spam_guard.cc
  dialog_spam_guard.h
  download_paths.cc
  download_paths.h
  external_protocol.cc
  external_protocol.h
  page_events.cc
  page_events.h
  permissions.cc
  permissions.h
  security_state.cc
  security_state.h
  tab_strip_model.cc
  tab_strip_model.h
  url_display.cc
  url_display.h
  zoom.cc
  zoom.h
)

# Built twice with different sanitizers (see BroukCompilerOptions.cmake):
# brouk_core for the app process, brouk_core_testing for the tests.
add_library(brouk_core STATIC ${BROUK_CORE_SOURCES})
brouk_target_options(brouk_core APP)

add_library(brouk_core_testing STATIC EXCLUDE_FROM_ALL ${BROUK_CORE_SOURCES})
brouk_target_options(brouk_core_testing)

foreach(target brouk_core brouk_core_testing)
  target_include_directories(${target} PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated")
endforeach()
