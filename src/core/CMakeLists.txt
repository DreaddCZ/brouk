# Portable core: no CEF, no platform headers. Builds and is tested on Linux.
include(BroukYaml)
include(BroukSqlite)

set(BROUK_CORE_SOURCES
  adblock_engine.cc
  adblock_engine.h
  adblock_lists.cc
  adblock_lists.h
  adblock_policy.cc
  adblock_policy.h
  adblock_store.cc
  adblock_store.h
  address_input.cc
  address_input.h
  address_suggestions.cc
  address_suggestions.h
  bookmark_file.cc
  bookmark_file.h
  bookmark_html.cc
  bookmark_html.h
  bookmark_model.cc
  bookmark_model.h
  bookmark_profile.cc
  bookmark_profile.h
  closed_tabs.cc
  closed_tabs.h
  command_palette.cc
  command_palette.h
  context_menu.cc
  context_menu.h
  crash_error_code.cc
  crash_error_code.h
  devtools_frontend.h
  devtools_tools.cc
  devtools_tools.h
  dialog_spam_guard.cc
  dialog_spam_guard.h
  download_paths.cc
  download_paths.h
  external_protocol.cc
  external_protocol.h
  history_query.cc
  history_query.h
  history_sql.cc
  history_sql.h
  history_store.cc
  history_store.h
  internal_pages.cc
  internal_pages.h
  json.cc
  json.h
  launch_args.cc
  launch_args.h
  page_events.cc
  page_events.h
  permissions.cc
  permissions.h
  profile_export.cc
  profile_export.h
  profile_id.h
  profile_registry.cc
  profile_registry.h
  request_log.cc
  request_log.h
  request_rules.cc
  request_rules.h
  result.h
  screen_share.cc
  search_engines.cc
  search_engines.h
  screen_share.h
  security_state.cc
  security_state.h
  service_workers.cc
  service_workers.h
  session.cc
  session.h
  settings.cc
  settings.h
  settings_export.cc
  settings_export.h
  settings_file.cc
  settings_file.h
  settings_schema.cc
  settings_schema.h
  shortcuts.cc
  shortcuts.h
  sqlite_db.cc
  sqlite_db.h
  tab_strip_layout.cc
  tab_strip_layout.h
  tab_strip_model.cc
  tab_strip_model.h
  url_display.cc
  url_display.h
  url_pattern.cc
  url_pattern.h
  yaml_io.cc
  yaml_io.h
  yaml_value.cc
  yaml_value.h
  zoom.cc
  zoom.h
)

# Built twice with different sanitizers (see BroukCompilerOptions.cmake):
# brouk_core for the app process, brouk_core_testing for the tests.
add_library(brouk_core STATIC ${BROUK_CORE_SOURCES})
brouk_target_options(brouk_core APP)

add_library(brouk_core_testing STATIC EXCLUDE_FROM_ALL ${BROUK_CORE_SOURCES})
brouk_target_options(brouk_core_testing)

# yaml-cpp throws; the one file using it catches everything (see the file).
set_source_files_properties(yaml_io.cc PROPERTIES COMPILE_OPTIONS -fexceptions)

target_link_libraries(brouk_core PRIVATE brouk_yaml_cpp brouk_sqlite)
target_link_libraries(brouk_core PUBLIC brouk_adblock_rs)
target_link_libraries(brouk_core_testing PRIVATE brouk_yaml_cpp_testing
  brouk_sqlite_testing)
target_link_libraries(brouk_core_testing PUBLIC brouk_adblock_rs)

foreach(target brouk_core brouk_core_testing)
  target_include_directories(${target} PUBLIC
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_BINARY_DIR}/generated")
endforeach()
