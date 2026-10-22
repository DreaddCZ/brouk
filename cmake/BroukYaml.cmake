# yaml-cpp, pinned by version and checksum (see third_party/README.md), for
# the settings files (spec 004). Only src/core/yaml_io.cc uses it; it throws
# exceptions, which that file catches.
#
# Built here from its sources, twice like the core (see
# BroukCompilerOptions.cmake): with the app's sanitizer flags and with the
# tests' flags. AddressSanitizer reports false "container-overflow" errors
# when instrumented and uninstrumented code share standard containers.
include(FetchContent)
FetchContent_Declare(yaml-cpp
  URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
  URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  # Download only; its own CMakeLists.txt is not used.
  SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(yaml-cpp)

file(GLOB BROUK_YAML_CPP_SOURCES "${yaml-cpp_SOURCE_DIR}/src/*.cpp")

# brouk_yaml_cpp_library(<target> <sanitizer flags...>)
function(brouk_yaml_cpp_library target)
  add_library(${target} STATIC EXCLUDE_FROM_ALL ${BROUK_YAML_CPP_SOURCES})
  target_compile_definitions(${target} PUBLIC YAML_CPP_STATIC_DEFINE)
  # Its headers are third-party code: not subject to our warnings.
  target_include_directories(${target} SYSTEM PUBLIC
    "${yaml-cpp_SOURCE_DIR}/include")
  target_include_directories(${target} PRIVATE "${yaml-cpp_SOURCE_DIR}/src")
  # No RTTI anywhere (see BroukCompilerOptions.cmake); its own warnings off.
  target_compile_options(${target} PRIVATE ${ARGN} -fno-rtti -w)
  target_link_options(${target} PRIVATE ${ARGN})
endfunction()

brouk_yaml_cpp_library(brouk_yaml_cpp ${BROUK_APP_SANITIZER_FLAGS})
brouk_yaml_cpp_library(brouk_yaml_cpp_testing ${BROUK_SANITIZER_FLAGS})
