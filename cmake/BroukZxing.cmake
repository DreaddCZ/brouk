# zxing-cpp, pinned by version and checksum (see third_party/README.md), for
# "Read QR code from this page" on Windows (spec 008); macOS uses Vision.
# Only src/platform/win/qr_reader.cc uses it; the library may throw, so that
# file is built with exceptions and catches them all.
#
# Built with its own CMake files: the readers only, QR codes only, as a
# static library. Its headers are third-party code (SYSTEM): not subject to
# our warnings.
include(FetchContent)
# As cache entries: its CMake files (policies of CMake 3.16) would replace
# plain variables with their own cached defaults.
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OFF CACHE STRING "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_DEPENDENCIES LOCAL CACHE STRING "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(format 1D AZTEC DATAMATRIX MAXICODE PDF417)
  set(ZXING_ENABLE_${format} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(zxing
  URL https://github.com/zxing-cpp/zxing-cpp/archive/refs/tags/v3.1.1.tar.gz
  URL_HASH SHA256=7286b1e6ade66fe82b7c8208b4595deeb55d6486b410834fdc65702f46650542
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_MakeAvailable(zxing)
# Its own warnings off, as for the other libraries; it throws, so with
# exceptions (the app's flags turn them off), as yaml-cpp's sources.
target_compile_options(ZXing PRIVATE ${BROUK_NO_WARNINGS_FLAG}
  ${BROUK_EXCEPTIONS_FLAG})
