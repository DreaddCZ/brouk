# Locates the CEF distribution fetched by scripts/fetch_cef and builds CEF's
# C++ wrapper library (libcef_dll_wrapper).

if(APPLE)
  if(NOT DEFINED PROJECT_ARCH)
    if(CMAKE_OSX_ARCHITECTURES)
      set(PROJECT_ARCH "${CMAKE_OSX_ARCHITECTURES}")
    else()
      set(PROJECT_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
    endif()
  endif()
  if(PROJECT_ARCH STREQUAL "arm64")
    set(BROUK_CEF_PLATFORM macosarm64)
  elseif(PROJECT_ARCH STREQUAL "x86_64")
    set(BROUK_CEF_PLATFORM macosx64)
  else()
    message(FATAL_ERROR "Unsupported macOS architecture: ${PROJECT_ARCH}")
  endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(BROUK_CEF_PLATFORM linux64)
elseif(WIN32)
  set(BROUK_CEF_PLATFORM windows64)
  # CEF adds /MT (/MTd in Debug); the runtime library is chosen for all our
  # targets in BroukCompilerOptions.cmake instead.
  set(CEF_RUNTIME_LIBRARY_FLAG "" CACHE STRING "" FORCE)
else()
  message(FATAL_ERROR "CEF platform not configured for ${CMAKE_SYSTEM_NAME}")
endif()

set(CEF_ROOT "${PROJECT_SOURCE_DIR}/third_party/cef/${BROUK_CEF_PLATFORM}")
if(NOT EXISTS "${CEF_ROOT}/cmake/FindCEF.cmake")
  message(FATAL_ERROR
    "CEF not found in ${CEF_ROOT}. Run: scripts/fetch_cef ${BROUK_CEF_PLATFORM}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

# CEF's CMake targets macOS 12 (and shadows CMAKE_OSX_DEPLOYMENT_TARGET with a
# normal variable); we require macOS 15 (002).
if(APPLE)
  list(REMOVE_ITEM CEF_COMPILER_FLAGS "-mmacosx-version-min=${CEF_TARGET_SDK}")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "$CACHE{CMAKE_OSX_DEPLOYMENT_TARGET}")
  set(CEF_TARGET_SDK "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  list(APPEND CEF_COMPILER_FLAGS
    "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

# CEF's MSVC flags that clang-cl does not support: /MP (cl.exe's own
# parallel build; Ninja runs compilers in parallel) and /RTC1 (cl.exe's
# run-time checks). Its /W4 /WX are tuned for cl.exe; under clang-cl they
# fail on CEF's own code, which is third-party code (no warnings, as
# BroukSodium.cmake). Our targets get our flags (BroukCompilerOptions.cmake).
if(MSVC)
  list(REMOVE_ITEM CEF_COMPILER_FLAGS /MP /W4 /WX)
  list(APPEND CEF_COMPILER_FLAGS /w)
  list(REMOVE_ITEM CEF_COMPILER_FLAGS_DEBUG /RTC1)
endif()

# The minimal distribution ships only Release binaries of CEF itself; our Debug
# builds use them too (our own code and the wrapper are still built as Debug).
set(BROUK_CEF_BINARY_DIR "${CEF_BINARY_DIR_RELEASE}")

add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper
                 EXCLUDE_FROM_ALL)

if(NOT APPLE)
  # On macOS the framework is loaded at runtime (CefScopedLibraryLoader).
  ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_RELEASE}" "${CEF_LIB_RELEASE}")
endif()
