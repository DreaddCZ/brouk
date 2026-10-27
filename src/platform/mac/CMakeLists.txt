# macOS app bundle:
#   <APP_NAME>.app/Contents/MacOS/<APP_EXECUTABLE_NAME>        browser process
#   <APP_NAME>.app/Contents/Frameworks/Chromium Embedded Framework.framework
#   <APP_NAME>.app/Contents/Frameworks/<APP_NAME> Helper*.app  child processes
#   <APP_NAME>.app/Contents/Frameworks/Sparkle.framework        updater (019)
# The bundle is assembled here directly (not with MACOSX_BUNDLE) so the bundle
# name and the executable name can differ ("Brouk.app" / "brouk").

set(APP_BUNDLE "${CMAKE_BINARY_DIR}/${APP_NAME}.app")
set(HELPER_NAME_BASE "${APP_NAME} Helper")

add_executable(brouk_app
  adblock_blocked_view.h
  adblock_blocked_view.mm
  adblock_popover.h
  adblock_popover.mm
  address_field.h
  address_field.mm
  bitwarden_dialogs.h
  bitwarden_dialogs.mm
  bookmark_dialogs.h
  bookmark_dialogs.mm
  bookmark_store.h
  bookmark_store.mm
  bookmarks_bar.h
  bookmarks_bar.mm
  address_suggestions_popup.h
  address_suggestions_popup.mm
  app_delegate.h
  autofill_prompt.h
  autofill_prompt.mm
  app_delegate.mm
  browser_tab.h
  browser_tab.mm
  browser_window.h
  browser_window.mm
  browser_window_controller.h
  browser_window_controller.mm
  command_palette_panel.h
  command_palette_panel.mm
  data_paths.h
  data_paths.mm
  devtools_dock.h
  devtools_dock.mm
  devtools_window.h
  devtools_window.mm
  favicon_cache.h
  favicon_cache.mm
  downloads_popover.h
  downloads_popover.mm
  export_password_dialog.h
  export_password_dialog.mm
  find_bar.h
  find_bar.mm
  help_button.h
  help_button.mm
  internal_page_window.h
  internal_page_window.mm
  keychain.h
  keychain.mm
  link_picker.h
  link_picker.mm
  link_resolver.h
  link_resolver.mm
  link_routing_controller.h
  link_routing_controller.mm
  main.mm
  password_menu.h
  password_menu.mm
  password_prompt.h
  password_prompt.mm
  menu_shortcuts.h
  menu_shortcuts.mm
  permission_prompt_view.h
  permission_prompt_view.mm
  profile_colors.h
  profile_colors.mm
  sad_tab_view.h
  sad_tab_view.mm
  screen_share_picker.h
  screen_share_picker.mm
  settings_watcher.h
  settings_watcher.mm
  site_info_popover.h
  site_info_popover.mm
  tab_dialog_view.h
  tab_dialog_view.mm
  tab_strip_view.h
  tab_strip_view.mm
  update_controller.h
  update_controller.mm
  window_screen.h
  window_screen.mm
)
brouk_target_options(brouk_app CEF APP)
target_link_libraries(brouk_app PRIVATE brouk_cef brouk_core
  "-framework AppKit" "-framework AVFoundation" "-framework Cocoa"
  "-framework CoreServices"
  "-framework IOSurface" "-framework LocalAuthentication"
  "-framework ScreenCaptureKit" "-framework Security"
  "-framework SecurityInterface" "-framework UniformTypeIdentifiers"
  "-framework Vision")
set_target_properties(brouk_app PROPERTIES
  OUTPUT_NAME "${APP_EXECUTABLE_NAME}"
  RUNTIME_OUTPUT_DIRECTORY "${APP_BUNDLE}/Contents/MacOS")

# Sparkle, the updater (019; scripts/fetch_sparkle). Every build links it;
# only builds with BROUK_UPDATES get a feed address and key, and without
# them the app never starts the updater.
set(_sparkle_dir "${PROJECT_SOURCE_DIR}/third_party/sparkle")
if(NOT EXISTS "${_sparkle_dir}/Sparkle.framework")
  message(FATAL_ERROR "Sparkle is missing; run scripts/fetch_sparkle.")
endif()
target_compile_options(brouk_app PRIVATE "-F${_sparkle_dir}")
target_link_options(brouk_app PRIVATE "-F${_sparkle_dir}" "-framework" "Sparkle"
  "-Wl,-rpath,@executable_path/../Frameworks")

option(BROUK_UPDATES "Automatic updates: feed address and key (scripts/release)" OFF)
set(BROUK_UPDATE_PLIST "")
if(BROUK_UPDATES)
  if(APP_UPDATE_PUBLIC_KEY STREQUAL "")
    message(FATAL_ERROR "APP_UPDATE_PUBLIC_KEY is not set in CMakeLists.txt.")
  endif()
  # Checks every 5 hours, no system profile (019, Chrome-like); whether
  # updates download and install by themselves is set at run time from
  # updates.mode.
  string(CONCAT BROUK_UPDATE_PLIST
    "\t<key>SUFeedURL</key>\n"
    "\t<string>${APP_UPDATE_FEED_BASE}/appcast-${CMAKE_OSX_ARCHITECTURES}.xml</string>\n"
    "\t<key>SUPublicEDKey</key>\n"
    "\t<string>${APP_UPDATE_PUBLIC_KEY}</string>\n"
    "\t<key>SUEnableAutomaticChecks</key>\n"
    "\t<true/>\n"
    "\t<key>SUScheduledCheckInterval</key>\n"
    "\t<integer>18000</integer>\n"
    "\t<key>SUEnableSystemProfiling</key>\n"
    "\t<false/>\n")
endif()
configure_file(Info.plist.in "${APP_BUNDLE}/Contents/Info.plist" @ONLY)

# One helper executable, copied into each helper bundle CEF expects. No
# sanitizers here: helpers run sandboxed and cannot load the sanitizer runtime.
add_executable(brouk_helper helper_main.cc)
brouk_target_options(brouk_helper CEF NO_SANITIZE)
target_link_libraries(brouk_helper PRIVATE brouk_renderer libcef_dll_wrapper)
set_target_properties(brouk_helper PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/helper")
add_dependencies(brouk_app brouk_helper)

# Assembling the bundle: copy the CEF framework and the helpers, then sign.
# A separate step (not POST_BUILD of brouk_app) so that it also runs when
# only the helper changed (renderer code), which does not relink the app.
set(_bundle_stamp "${CMAKE_CURRENT_BINARY_DIR}/bundle.stamp")
set(_bundle_commands "")

# Internal pages (web/, TypeScript), compiled into the bundle's resources.
file(GLOB_RECURSE _web_sources CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/web/shared/*" "${PROJECT_SOURCE_DIR}/web/history/*"
  "${PROJECT_SOURCE_DIR}/web/bookmarks/*" "${PROJECT_SOURCE_DIR}/web/downloads/*"
  "${PROJECT_SOURCE_DIR}/web/forms/*" "${PROJECT_SOURCE_DIR}/web/passwords/*"
  "${PROJECT_SOURCE_DIR}/web/help/*"
  "${PROJECT_SOURCE_DIR}/web/profiles/*"
  "${PROJECT_SOURCE_DIR}/web/settings/*" "${PROJECT_SOURCE_DIR}/web/tools/*"
  "${PROJECT_SOURCE_DIR}/web/jsonview/*")
set(_web_stamp "${CMAKE_CURRENT_BINARY_DIR}/web.stamp")
add_custom_command(OUTPUT "${_web_stamp}"
  COMMAND "${PROJECT_SOURCE_DIR}/scripts/web" build
          "${APP_BUNDLE}/Contents/Resources/web"
  COMMAND ${CMAKE_COMMAND} -E touch "${_web_stamp}"
  DEPENDS ${_web_sources} "${PROJECT_SOURCE_DIR}/web/tsconfig.json"
          "${PROJECT_SOURCE_DIR}/web/package-lock.json"
          "${PROJECT_SOURCE_DIR}/scripts/vendor_modules.mjs"
          "${PROJECT_SOURCE_DIR}/src/core/settings_schema.cc"
  COMMENT "Building internal pages"
  JOB_POOL brouk_web
  VERBATIM)

# Copy the CEF framework. (CEF's COPY_MAC_FRAMEWORK macro uses "ln -sf", which
# on a rebuild follows the existing directory links and nests new links inside
# the framework, breaking its signature; "ln -sfn" replaces them.)
set(_framework
  "${APP_BUNDLE}/Contents/Frameworks/Chromium Embedded Framework.framework")
list(APPEND _bundle_commands
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${BROUK_CEF_BINARY_DIR}/Chromium Embedded Framework.framework"
          "${_framework}/Versions/A"
  COMMAND ln -sfn A "${_framework}/Versions/Current"
  COMMAND ln -sfn "Versions/Current/Chromium Embedded Framework"
          "${_framework}/Chromium Embedded Framework"
  COMMAND ln -sfn Versions/Current/Libraries "${_framework}/Libraries"
  COMMAND ln -sfn Versions/Current/Resources "${_framework}/Resources")

# Sparkle, only this build's architecture (019: each app holds only its
# own). Its XPC services are only for sandboxed apps; Brouk is not.
set(_sparkle "${APP_BUNDLE}/Contents/Frameworks/Sparkle.framework")
list(APPEND _bundle_commands
  COMMAND rm -rf "${_sparkle}"
  COMMAND ditto --arch "${CMAKE_OSX_ARCHITECTURES}"
          "${_sparkle_dir}/Sparkle.framework" "${_sparkle}"
  COMMAND rm -rf "${_sparkle}/Versions/B/XPCServices" "${_sparkle}/XPCServices")

# Ad blocking resources (007), the first copy (see BroukAdblock.cmake).
list(APPEND _bundle_commands
  COMMAND ${CMAKE_COMMAND} -E copy "${BROUK_ADBLOCK_RESOURCES}"
          "${APP_BUNDLE}/Contents/Resources/adblock-resources.json")

set(_signed_helpers "")
set(_helper_kinds "")
foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
  # Entries look like " (GPU):_gpu:.gpu" (name suffix, target, bundle id).
  string(REPLACE ":" ";" _suffix_list "${_suffix_list}")
  list(GET _suffix_list 0 _name_suffix)
  list(GET _suffix_list 1 _target_suffix)
  list(GET _suffix_list 2 HELPER_BUNDLE_ID_SUFFIX)
  set(HELPER_NAME "${HELPER_NAME_BASE}${_name_suffix}")
  set(_helper_app "${APP_BUNDLE}/Contents/Frameworks/${HELPER_NAME}.app")
  configure_file(helper-Info.plist.in "${_helper_app}/Contents/Info.plist" @ONLY)
  list(APPEND _bundle_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_helper_app}/Contents/MacOS"
    COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:brouk_helper>"
            "${_helper_app}/Contents/MacOS/${HELPER_NAME}")
  list(APPEND _signed_helpers "${_helper_app}")
  list(APPEND _helper_kinds "helper${_target_suffix}")
endforeach()

# Signing. Every build is signed with a certificate that stays the same
# across builds, so macOS remembers keychain access and permissions. The
# keychain remembers apps by team ID when the certificate has one, so Apple
# certificates of the team are preferred: then local builds and the
# notarized releases (Developer ID, scripts/package_mac) count as the same
# app, and rebuilding asks for nothing. Local builds use the "Apple
# Development" certificate, because macOS refuses to load the sanitizer
# runtime of debug builds into a Developer ID app. Without an Apple
# certificate: the self-signed one from scripts/create_dev_certificate (no
# team ID, so macOS asks again after each rebuild), else ad-hoc.
# BROUK_SIGN_IDENTITY (scripts/build --sign) picks another certificate.
set(BROUK_SIGN_IDENTITY "" CACHE STRING
    "Code signing identity (empty: chosen automatically)")
option(BROUK_HARDENED_RUNTIME
       "Sign for notarization: hardened runtime, timestamp, entitlements" OFF)
execute_process(COMMAND security find-identity -v -p codesigning
                OUTPUT_VARIABLE _identities ERROR_QUIET)
# Certificates by their SHA-1 hash: the same name can exist twice (one
# Apple Development certificate per Mac).
function(brouk_find_identity name_prefix out)
  string(REGEX MATCH "([0-9A-F]+) \"${name_prefix}[^\"]*\"" _match
         "${_identities}")
  set(${out} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()
brouk_find_identity("Apple Development: " _apple_development)
brouk_find_identity("Developer ID Application: " _developer_id)
brouk_find_identity("${APP_NAME} Local Dev" _local_dev)
if(NOT BROUK_SIGN_IDENTITY STREQUAL "")
  set(_identity "${BROUK_SIGN_IDENTITY}")
elseif(_apple_development)
  set(_identity "${_apple_development}")
elseif(_developer_id AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_identity "${_developer_id}")
elseif(_local_dev)
  set(_identity "${_local_dev}")
else()
  set(_identity "-")
  message(STATUS "Signing ad-hoc; run scripts/create_dev_certificate to "
                 "keep keychain access and permissions across builds.")
endif()

# Releases for other Macs (scripts/package_mac) also need, for notarization,
# the hardened runtime, a secure timestamp and entitlements for what Chromium
# needs under that runtime (JIT for V8, camera, microphone…; as Chrome's own,
# see entitlements/). Local builds go without: the hardened runtime would
# block the sanitizer runtime and debuggers.
set(_sign codesign --force --sign "${_identity}")
set(_entitlements_dir "${CMAKE_CURRENT_SOURCE_DIR}/entitlements")
set(_signing_inputs "")
if(BROUK_HARDENED_RUNTIME)
  list(APPEND _sign --options runtime --timestamp)
  file(GLOB _signing_inputs "${_entitlements_dir}/*.entitlements")
endif()
# Re-sign when the identity changes, not only when the code does.
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/signing_identity.txt"
     CONTENT "${_identity} ${BROUK_HARDENED_RUNTIME} ${BROUK_UPDATES}\n")
list(APPEND _signing_inputs "${CMAKE_CURRENT_BINARY_DIR}/signing_identity.txt")

# Inner code first, then the app.
list(APPEND _bundle_commands
  COMMAND ${_sign}
          "${_framework}/Libraries/libcef_sandbox.dylib"
          "${_framework}/Libraries/libvk_swiftshader.dylib"
          "${_framework}/Libraries/libvulkan.dylib"
  COMMAND ${_sign} "${_framework}"
  COMMAND ${_sign} "${_sparkle}/Versions/B/Autoupdate"
  COMMAND ${_sign} "${_sparkle}/Versions/B/Updater.app"
  COMMAND ${_sign} "${_sparkle}")
foreach(_helper_app _kind IN ZIP_LISTS _signed_helpers _helper_kinds)
  set(_helper_sign ${_sign})
  if(BROUK_HARDENED_RUNTIME AND EXISTS "${_entitlements_dir}/${_kind}.entitlements")
    list(APPEND _helper_sign
         --entitlements "${_entitlements_dir}/${_kind}.entitlements")
  endif()
  list(APPEND _bundle_commands COMMAND ${_helper_sign} "${_helper_app}")
endforeach()
set(_app_sign ${_sign})
if(BROUK_HARDENED_RUNTIME)
  list(APPEND _app_sign --entitlements "${_entitlements_dir}/app.entitlements")
endif()
list(APPEND _bundle_commands
  COMMAND ${_app_sign} "${APP_BUNDLE}"
  COMMAND ${CMAKE_COMMAND} -E touch "${_bundle_stamp}")

add_custom_command(OUTPUT "${_bundle_stamp}"
  ${_bundle_commands}
  DEPENDS brouk_app brouk_helper "${_web_stamp}"
          "$<TARGET_FILE:brouk_app>" "$<TARGET_FILE:brouk_helper>"
          ${_signing_inputs}
  COMMENT "Assembling and signing ${APP_NAME}.app (${_identity})"
  VERBATIM)
add_custom_target(brouk_bundle ALL DEPENDS "${_bundle_stamp}")
