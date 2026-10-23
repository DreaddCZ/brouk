# SQLite, the official amalgamation pinned by version and checksum (see
# third_party/README.md), for the history databases (specs 013, 003). Only
# the core's history storage uses it.
#
# Built twice like the core (see BroukCompilerOptions.cmake): with the app's
# sanitizer flags and with the tests' flags.
include(FetchContent)
FetchContent_Declare(sqlite
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
  URL_HASH SHA256=0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(sqlite)
find_package(Threads REQUIRED)

# brouk_sqlite_library(<target> <sanitizer flags...>)
function(brouk_sqlite_library target)
  add_library(${target} STATIC EXCLUDE_FROM_ALL "${sqlite_SOURCE_DIR}/sqlite3.c")
  # Its header is third-party code: not subject to our warnings.
  target_include_directories(${target} SYSTEM PUBLIC "${sqlite_SOURCE_DIR}")
  # SQLite's recommended options: no double-quoted strings, no deprecated
  # APIs, no loadable extensions; thread-safe; WAL databases sync less often
  # (a crash loses at most the last transactions, never the database).
  target_compile_definitions(${target} PRIVATE
    SQLITE_DQS=0
    SQLITE_THREADSAFE=1
    SQLITE_MAX_ATTACHED=64
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
    SQLITE_LIKE_DOESNT_MATCH_BLOBS
    SQLITE_OMIT_DEPRECATED
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_OMIT_SHARED_CACHE
    SQLITE_USE_ALLOCA)
  target_compile_options(${target} PRIVATE ${ARGN} -w)
  target_link_options(${target} PRIVATE ${ARGN})
  target_link_libraries(${target} PUBLIC Threads::Threads)
endfunction()

brouk_sqlite_library(brouk_sqlite ${BROUK_APP_SANITIZER_FLAGS})
brouk_sqlite_library(brouk_sqlite_testing ${BROUK_SANITIZER_FLAGS})
