# Windows app (024). The app folder <build>/<APP_NAME>/ holds:
#   <APP_EXECUTABLE_NAME>.exe  CEF's bootstrap.exe, renamed: starts the
#                              sandbox, then calls RunWinMain in the DLL
#   <APP_EXECUTABLE_NAME>.dll  Brouk itself (main.cc), for every process
#   libcef.dll, CEF's other binaries, resources and locales
# This is CEF's sandboxed layout on Windows (cefsimple with USE_SANDBOX):
# the sandbox has to start in the executable, which CEF provides prebuilt.

set(APP_DIR "${CMAKE_BINARY_DIR}/${APP_NAME}")

add_library(brouk_app SHARED
  app.cc
  app.h
  bookmark_dialog.cc
  bookmark_dialog.h
  bookmark_store.cc
  bookmark_store.h
  browser_window.cc
  browser_window.h
  controls.cc
  controls.h
  data_paths.cc
  data_paths.h
  desktop_sources.cc
  desktop_sources.h
  file_dialogs.cc
  file_dialogs.h
  favicon.cc
  favicon.h
  find_bar.cc
  find_bar.h
  internal_page_window.cc
  internal_page_window.h
  main.cc
  settings_watcher.cc
  settings_watcher.h
  suggestions_popup.cc
  suggestions_popup.h
  tab.cc
  tab.h
  tab_panel.cc
  tab_panel.h
  tab_strip.cc
  tab_strip.h
  theme.cc
  theme.h
  wide_string.cc
  wide_string.h
)
brouk_target_options(brouk_app CEF APP)
target_link_libraries(brouk_app PRIVATE
  brouk_cef brouk_renderer brouk_core libcef_lib ${CEF_STANDARD_LIBS}
  dwmapi msimg32 ole32 oleacc shell32 shlwapi user32 uuid uxtheme windowscodecs)
# Chromium's delay-loaded system DLLs: some must not load in sandboxed
# processes, which run this DLL too.
target_link_options(brouk_app PRIVATE ${CEF_SHARED_LINKER_FLAGS})
set_target_properties(brouk_app PROPERTIES
  OUTPUT_NAME "${APP_EXECUTABLE_NAME}"
  RUNTIME_OUTPUT_DIRECTORY "${APP_DIR}"
  # The import library is not part of the app.
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")

# CEF's files next to the DLL, and the bootstrap executable as brouk.exe.
set(_copy_commands
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${BROUK_CEF_BINARY_DIR}/bootstrap.exe"
          "${APP_DIR}/${APP_EXECUTABLE_NAME}.exe")
foreach(_file IN LISTS CEF_BINARY_FILES)
  list(APPEND _copy_commands
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BROUK_CEF_BINARY_DIR}/${_file}" "${APP_DIR}/${_file}")
endforeach()
foreach(_file IN LISTS CEF_RESOURCE_FILES)
  if(IS_DIRECTORY "${CEF_RESOURCE_DIR}/${_file}")
    list(APPEND _copy_commands
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${CEF_RESOURCE_DIR}/${_file}" "${APP_DIR}/${_file}")
  else()
    list(APPEND _copy_commands
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${CEF_RESOURCE_DIR}/${_file}" "${APP_DIR}/${_file}")
  endif()
endforeach()
add_custom_command(TARGET brouk_app POST_BUILD
  ${_copy_commands}
  # Sandboxed processes running as a less privileged app container (LPAC)
  # must be able to read the app folder (CEF's SET_LPAC_ACLS).
  COMMAND icacls "${APP_DIR}" /grant "*S-1-15-2-2:(OI)(CI)(RX)" /Q
  COMMENT "Assembling ${APP_NAME}"
  VERBATIM)

# Internal pages (web/, TypeScript) and the open-source licences (023), built
# into <app>/web. scripts/web and scripts/credits are bash scripts, run with
# Git's bash (see src/renderer/CMakeLists.txt).
file(GLOB_RECURSE _web_sources CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/web/shared/*" "${PROJECT_SOURCE_DIR}/web/history/*"
  "${PROJECT_SOURCE_DIR}/web/bookmarks/*" "${PROJECT_SOURCE_DIR}/web/credits/*"
  "${PROJECT_SOURCE_DIR}/web/downloads/*"
  "${PROJECT_SOURCE_DIR}/web/forms/*" "${PROJECT_SOURCE_DIR}/web/passwords/*"
  "${PROJECT_SOURCE_DIR}/web/help/*"
  "${PROJECT_SOURCE_DIR}/web/profiles/*"
  "${PROJECT_SOURCE_DIR}/web/settings/*" "${PROJECT_SOURCE_DIR}/web/tools/*"
  "${PROJECT_SOURCE_DIR}/web/task-manager/*"
  "${PROJECT_SOURCE_DIR}/web/jsonview/*")
set(_web_stamp "${CMAKE_CURRENT_BINARY_DIR}/web.stamp")
add_custom_command(OUTPUT "${_web_stamp}"
  COMMAND "${BROUK_BASH}" "${PROJECT_SOURCE_DIR}/scripts/web" build
          "${APP_DIR}/web"
  # The licences of everything third-party in the app; fails the build if
  # one is missing.
  COMMAND "${BROUK_BASH}" "${PROJECT_SOURCE_DIR}/scripts/credits"
          "${APP_DIR}/web/credits"
          --cef "${CEF_ROOT}" --deps "${CMAKE_BINARY_DIR}/_deps"
          --cargo "${BROUK_CARGO}" --cargo-home "${PROJECT_SOURCE_DIR}/.tools/cargo"
          --rust-target "${BROUK_RUST_TARGET}"
  COMMAND ${CMAKE_COMMAND} -E touch "${_web_stamp}"
  DEPENDS ${_web_sources} "${PROJECT_SOURCE_DIR}/web/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/scripts/credits.mjs"
          "${PROJECT_SOURCE_DIR}/third_party/README.md"
          "${PROJECT_SOURCE_DIR}/src/adblock/Cargo.lock"
          "${BROUK_ADBLOCK_LIBRARY}"
          "${PROJECT_SOURCE_DIR}/web/package-lock.json"
          "${PROJECT_SOURCE_DIR}/scripts/vendor_modules.mjs"
          "${PROJECT_SOURCE_DIR}/src/core/settings_schema.cc"
  COMMENT "Building internal pages"
  JOB_POOL brouk_web
  VERBATIM)
add_custom_target(brouk_web_pages DEPENDS "${_web_stamp}")
add_dependencies(brouk_app brouk_web_pages)
