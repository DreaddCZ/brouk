# yaml-cpp, pinned by version and checksum (see third_party/README.md), for
# the settings files (spec 004). Only src/core/yaml_io.cc uses it; it throws
# exceptions, which that file catches.
include(FetchContent)
FetchContent_Declare(yaml-cpp
  URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
  URL_HASH SHA256=fbe74bbdcee21d656715688706da3c8becfd946d92cd44705cc6098bb23b3a16
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  # Its headers are third-party code: not subject to our warnings.
  SYSTEM
  EXCLUDE_FROM_ALL)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# yaml-cpp 0.8.0 declares cmake_minimum_required(3.4), which CMake 4 rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(yaml-cpp)
unset(CMAKE_POLICY_VERSION_MINIMUM)
# No RTTI anywhere (see BroukCompilerOptions.cmake); no sanitizers, so the
# same build links into the app (UBSan) and the tests (ASan + UBSan).
target_compile_options(yaml-cpp PRIVATE -fno-rtti -w)
