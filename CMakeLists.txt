cmake_minimum_required(VERSION 3.25)

# App identity: the only place these names are defined (see CLAUDE.md,
# "Decisions"). Code gets them from the generated header brouk/app_info.h.
set(APP_NAME "Brouk")
set(APP_EXECUTABLE_NAME "brouk")
set(APP_BUNDLE_ID "io.github.dreaddcz.brouk")
set(APP_DATA_DIR_NAME "Brouk")
set(APP_VERSION "0.1.0")

# macOS 15 Sequoia is the minimum (002). Must be set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")

project(Brouk VERSION ${APP_VERSION} LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BroukCompilerOptions)

# The CEF integration and the app need the CEF distribution (scripts/fetch_cef).
# The core and its tests build without it.
option(BROUK_WITH_CEF "Build the CEF integration and the app" ON)

configure_file(src/app_info.h.in
               "${CMAKE_CURRENT_BINARY_DIR}/generated/brouk/app_info.h" @ONLY)

# The ad blocking engine (Rust, 007), used by the core.
include(BroukAdblock)

add_subdirectory(src/core)

if(BROUK_WITH_CEF)
  include(BroukCef)
  add_subdirectory(src/cef)
  add_subdirectory(src/renderer)
  if(APPLE)
    add_subdirectory(src/platform/mac)
  endif()
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
