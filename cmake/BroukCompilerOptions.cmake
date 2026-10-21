# Strict compiler settings for our own targets (not for third-party code).
# See CLAUDE.md, "C++ rules".

option(BROUK_SANITIZE
       "Build with AddressSanitizer + UndefinedBehaviorSanitizer (Debug default)"
       OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT DEFINED CACHE{BROUK_SANITIZE_SET})
  set(BROUK_SANITIZE ON CACHE BOOL "" FORCE)
  set(BROUK_SANITIZE_SET ON CACHE INTERNAL "")
endif()

option(BROUK_TSAN "Build with ThreadSanitizer instead of ASan/UBSan" OFF)

set(BROUK_WARNING_FLAGS
  -Wall -Wextra -Wpedantic -Wshadow -Werror
  -Wnon-virtual-dtor -Woverloaded-virtual -Wnull-dereference
  -Wimplicit-fallthrough)

if(BROUK_TSAN)
  set(BROUK_SANITIZER_FLAGS -fsanitize=thread)
  set(BROUK_APP_SANITIZER_FLAGS ${BROUK_SANITIZER_FLAGS})
elseif(BROUK_SANITIZE)
  set(BROUK_SANITIZER_FLAGS
    -fsanitize=address,undefined -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer)
  # The app process loads the CEF framework, which traps at startup under
  # AddressSanitizer (seen with CEF 154 on macOS); the app gets UBSan only.
  # Everything testable without CEF runs under ASan in the tests.
  set(BROUK_APP_SANITIZER_FLAGS
    -fsanitize=undefined -fno-sanitize-recover=undefined
    -fno-omit-frame-pointer)
else()
  set(BROUK_SANITIZER_FLAGS "")
  set(BROUK_APP_SANITIZER_FLAGS "")
endif()

# Applies warnings, sanitizers and exception settings to one of our targets.
#   brouk_target_options(<target> [CEF] [APP] [NO_SANITIZE])
# CEF: the target uses CEF headers; compile like CEF's own code (no RTTI) and
# treat the CEF headers as system headers so -Wpedantic does not flag them.
# APP: code linked into the app process (see BROUK_APP_SANITIZER_FLAGS).
# NO_SANITIZE: for code that cannot load the sanitizer runtime (sandboxed
# helper processes).
function(brouk_target_options target)
  cmake_parse_arguments(ARG "CEF;APP;NO_SANITIZE" "" "" ${ARGN})
  set(sanitizer_flags ${BROUK_SANITIZER_FLAGS})
  if(ARG_APP)
    set(sanitizer_flags ${BROUK_APP_SANITIZER_FLAGS})
  endif()
  if(ARG_NO_SANITIZE)
    set(sanitizer_flags "")
  endif()
  target_compile_options(${target} PRIVATE
    ${BROUK_WARNING_FLAGS} ${sanitizer_flags}
    # We never throw (CLAUDE.md, "Errors").
    $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    # "a ?: b" is idiomatic Objective-C; allow it there only.
    $<$<COMPILE_LANGUAGE:OBJCXX>:-Wno-gnu-conditional-omitted-operand>)
  target_link_options(${target} PRIVATE ${sanitizer_flags})
  target_include_directories(${target} PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated")
  if(ARG_CEF)
    target_compile_options(${target} PRIVATE
      $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-fno-rtti>)
    target_include_directories(${target} SYSTEM PRIVATE "${CEF_ROOT}")
    target_compile_definitions(${target} PRIVATE ${CEF_COMPILER_DEFINES})
  endif()
endfunction()
