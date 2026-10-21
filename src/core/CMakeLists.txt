# Portable core: no CEF, no platform headers. Builds and is tested on Linux.
set(BROUK_CORE_SOURCES
  address_input.cc
  address_input.h
  tab_strip_model.cc
  tab_strip_model.h
  url_display.cc
  url_display.h
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
