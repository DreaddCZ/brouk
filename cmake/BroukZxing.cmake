# zxing-cpp, pinned by version and checksum (see third_party/README.md), for
# "Read QR code from this page" on Windows (spec 008); macOS uses Vision.
# Only src/platform/win/qr_reader.cc uses it; the library may throw, so that
# file is built with exceptions and catches them all.
#
# Built with its own CMake files: the readers only, QR codes only, as a
# static library. Its headers are third-party code (SYSTEM): not subject to
# our warnings.
include(FetchContent)
set(ZXING_READERS ON)
set(ZXING_WRITERS OFF)
set(ZXING_C_API OFF)
set(ZXING_EXAMPLES OFF)
set(ZXING_DEPENDENCIES LOCAL)
set(BUILD_SHARED_LIBS OFF)
foreach(format 1D AZTEC DATAMATRIX MAXICODE PDF417)
  set(ZXING_ENABLE_${format} OFF)
endforeach()
FetchContent_Declare(zxing
  URL https://github.com/zxing-cpp/zxing-cpp/archive/refs/tags/v3.1.1.tar.gz
  URL_HASH SHA256=7286b1e6ade66fe82b7c8208b4595deeb55d6486b410834fdc65702f46650542
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SYSTEM)
FetchContent_MakeAvailable(zxing)
# Its own warnings off, as for the other libraries.
target_compile_options(ZXing PRIVATE ${BROUK_NO_WARNINGS_FLAG})
