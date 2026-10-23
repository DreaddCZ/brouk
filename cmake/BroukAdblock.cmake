# Brave's adblock-rust (MPL-2.0, specs/007) behind our thin C wrapper in
# src/adblock, built with the pinned Rust toolchain (scripts/install_tools).
# Cargo.lock pins every crate by version and checksum. Always built in
# release mode: request matching must stay well under 1 ms (007).
set(BROUK_ADBLOCK_DIR "${PROJECT_SOURCE_DIR}/src/adblock")
set(BROUK_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo")
set(BROUK_ADBLOCK_LIBRARY
  "${BROUK_CARGO_TARGET_DIR}/release/${CMAKE_STATIC_LIBRARY_PREFIX}brouk_adblock${CMAKE_STATIC_LIBRARY_SUFFIX}")

find_program(BROUK_CARGO cargo REQUIRED
  HINTS "${PROJECT_SOURCE_DIR}/.tools/bin" NO_DEFAULT_PATH)

set(_adblock_env "CARGO_HOME=${PROJECT_SOURCE_DIR}/.tools/cargo")
if(APPLE)
  list(APPEND _adblock_env "MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

add_custom_command(
  OUTPUT "${BROUK_ADBLOCK_LIBRARY}"
  COMMAND ${CMAKE_COMMAND} -E env ${_adblock_env}
          "${BROUK_CARGO}" build --release --locked --quiet
          --manifest-path "${BROUK_ADBLOCK_DIR}/Cargo.toml"
          --target-dir "${BROUK_CARGO_TARGET_DIR}"
  DEPENDS "${BROUK_ADBLOCK_DIR}/Cargo.toml" "${BROUK_ADBLOCK_DIR}/Cargo.lock"
          "${BROUK_ADBLOCK_DIR}/src/lib.rs"
  COMMENT "Building the ad blocking engine (Rust)"
  VERBATIM)
add_custom_target(brouk_adblock_build DEPENDS "${BROUK_ADBLOCK_LIBRARY}")

add_library(brouk_adblock_rs STATIC IMPORTED GLOBAL)
set_target_properties(brouk_adblock_rs PROPERTIES
  IMPORTED_LOCATION "${BROUK_ADBLOCK_LIBRARY}")
add_dependencies(brouk_adblock_rs brouk_adblock_build)
# What Rust's standard library needs from the system (cargo's
# native-static-libs; on macOS libSystem, which is linked anyway).
if(NOT APPLE)
  find_package(Threads REQUIRED)
  target_link_libraries(brouk_adblock_rs INTERFACE Threads::Threads dl m)
endif()
target_include_directories(brouk_adblock_rs SYSTEM INTERFACE "${BROUK_ADBLOCK_DIR}/include")

# Brave's adblock-resources (MPL-2.0): redirect and scriptlet resources,
# pinned by commit and checksum. Bundled with the app as the first copy;
# newer copies are downloaded with the lists (007).
set(BROUK_ADBLOCK_RESOURCES "${CMAKE_BINARY_DIR}/adblock-resources.json")
if(NOT EXISTS "${BROUK_ADBLOCK_RESOURCES}")
  file(DOWNLOAD
    "https://raw.githubusercontent.com/brave/adblock-resources/60346357addf85035833f3731cb025b058f9600d/dist/resources.json"
    "${BROUK_ADBLOCK_RESOURCES}"
    EXPECTED_HASH SHA256=d085eb0754b7a54d9e1a18a0314b1946b066f61a15457de834d98beb53b4e40f
    TLS_VERIFY ON)
endif()
