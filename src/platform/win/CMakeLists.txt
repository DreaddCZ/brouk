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
  browser_window.cc
  browser_window.h
  data_paths.cc
  data_paths.h
  main.cc
  tab.cc
  tab.h
  wide_string.cc
  wide_string.h
)
brouk_target_options(brouk_app CEF APP)
target_link_libraries(brouk_app PRIVATE
  brouk_cef brouk_renderer brouk_core libcef_lib ${CEF_STANDARD_LIBS}
  ole32 oleacc shell32 user32)
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
