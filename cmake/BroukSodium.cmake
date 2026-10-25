# libsodium, pinned by version and checksum (see third_party/README.md), for
# encrypting saved passwords and autofill data (specs 008, 009).
#
# libsodium builds with autotools; we compile its sources directly with the
# configuration its own Zig build uses for macOS and Linux, without the
# CPU-specific assembly and SIMD variants (the portable code is fast enough
# for a few thousand small values). Built twice like the core (see
# BroukCompilerOptions.cmake): with the app's and with the tests' sanitizer
# flags.
include(FetchContent)
FetchContent_Declare(sodium
  URL https://download.libsodium.org/libsodium/releases/libsodium-1.0.22.tar.gz
  URL_HASH SHA256=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sodium)
find_package(Threads REQUIRED)

set(_sodium_src "${sodium_SOURCE_DIR}/src/libsodium")
# The release ships version.h as a template; its prebuilt copy is the same
# file the Zig and MSVC builds use.
configure_file("${sodium_SOURCE_DIR}/builds/msvc/version.h"
  "${_sodium_src}/include/sodium/version.h" COPYONLY)
file(GLOB_RECURSE BROUK_SODIUM_SOURCES CONFIGURE_DEPENDS "${_sodium_src}/*.c")

if(MSVC)
  # As libsodium's own MSVC projects (builds/msvc): the Windows code paths
  # need no feature macros beyond these.
  set(BROUK_SODIUM_DEFINES
    CONFIGURED=1
    NATIVE_LITTLE_ENDIAN=1
    HAVE_INTTYPES_H=1
    HAVE_STDINT_H=1
    HAVE_RAISE=1
    _CRT_SECURE_NO_WARNINGS)
  set(BROUK_SODIUM_FLAGS /w /clang:-fno-strict-aliasing
    /clang:-fno-strict-overflow /clang:-fwrapv)
else()
  set(BROUK_SODIUM_DEFINES
    CONFIGURED=1
    NATIVE_LITTLE_ENDIAN=1
    HAVE_INTTYPES_H=1
    HAVE_STDINT_H=1
    HAVE_TI_MODE=1
    HAVE_ATOMIC_OPS=1
    HAVE_C11_MEMORY_FENCES=1
    HAVE_GCC_MEMORY_FENCES=1
    TLS=_Thread_local
    HAVE_CLOCK_GETTIME=1
    HAVE_GETPID=1
    HAVE_MADVISE=1
    HAVE_MLOCK=1
    HAVE_MMAP=1
    HAVE_MPROTECT=1
    HAVE_NANOSLEEP=1
    HAVE_POSIX_MEMALIGN=1
    HAVE_PTHREAD=1
    HAVE_RAISE=1
    HAVE_SYSCONF=1
    HAVE_SYS_MMAN_H=1
    HAVE_SYS_PARAM_H=1
    HAVE_SYS_RANDOM_H=1
    HAVE_GETENTROPY=1)
  if(APPLE)
    list(APPEND BROUK_SODIUM_DEFINES HAVE_ARC4RANDOM=1 HAVE_ARC4RANDOM_BUF=1
      HAVE_MEMSET_S=1)
  else()
    list(APPEND BROUK_SODIUM_DEFINES _GNU_SOURCE=1)
  endif()
  set(BROUK_SODIUM_FLAGS -w -fvisibility=hidden -fno-strict-aliasing
    -fno-strict-overflow -fwrapv)
endif()

# brouk_sodium_library(<target> <sanitizer flags...>)
function(brouk_sodium_library target)
  add_library(${target} STATIC EXCLUDE_FROM_ALL ${BROUK_SODIUM_SOURCES})
  # Its headers are third-party code: not subject to our warnings.
  target_include_directories(${target} SYSTEM PUBLIC "${_sodium_src}/include")
  target_include_directories(${target} PRIVATE "${_sodium_src}/include/sodium")
  target_compile_definitions(${target} PUBLIC SODIUM_STATIC=1)
  target_compile_definitions(${target} PRIVATE ${BROUK_SODIUM_DEFINES})
  target_compile_options(${target} PRIVATE ${ARGN} ${BROUK_SODIUM_FLAGS})
  target_link_options(${target} PRIVATE ${ARGN})
  target_link_libraries(${target} PUBLIC Threads::Threads)
endfunction()

brouk_sodium_library(brouk_sodium ${BROUK_APP_SANITIZER_FLAGS})
brouk_sodium_library(brouk_sodium_testing ${BROUK_SANITIZER_FLAGS})
