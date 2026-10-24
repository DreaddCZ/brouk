include(FetchContent)

# GoogleTest, pinned by version and checksum (see third_party/README.md).
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/releases/download/v1.18.0/googletest-1.18.0.tar.gz
  URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  EXCLUDE_FROM_ALL)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
# GoogleTest must match our sanitizer and RTTI settings.
foreach(gtest_target gtest gtest_main)
  target_compile_options(${gtest_target} PRIVATE ${BROUK_SANITIZER_FLAGS}
                         -fno-rtti)
endforeach()

include(GoogleTest)

add_executable(brouk_core_tests
  core/adblock_engine_test.cc
  core/adblock_lists_test.cc
  core/adblock_policy_test.cc
  core/adblock_store_test.cc
  core/address_input_test.cc
  core/address_suggestions_test.cc
  core/bookmark_file_test.cc
  core/bookmark_html_test.cc
  core/bookmark_model_test.cc
  core/bookmark_profile_test.cc
  core/closed_tabs_test.cc
  core/command_palette_test.cc
  core/context_menu_test.cc
  core/crash_error_code_test.cc
  core/crypto_test.cc
  core/devtools_tools_test.cc
  core/dialog_spam_guard_test.cc
  core/download_paths_test.cc
  core/external_protocol_test.cc
  core/history_query_test.cc
  core/history_sql_test.cc
  core/history_store_test.cc
  core/internal_pages_test.cc
  core/json_test.cc
  core/launch_args_test.cc
  core/page_events_test.cc
  core/password_checkup_test.cc
  core/password_csv_test.cc
  core/password_generator_test.cc
  core/password_profile_test.cc
  core/password_store_test.cc
  core/permissions_test.cc
  core/profile_export_test.cc
  core/profile_registry_test.cc
  core/request_log_test.cc
  core/request_rules_test.cc
  core/screen_share_test.cc
  core/search_engines_test.cc
  core/secret_store_test.cc
  core/secure_dns_test.cc
  core/security_state_test.cc
  core/service_workers_test.cc
  core/session_test.cc
  core/settings_export_test.cc
  core/settings_file_test.cc
  core/settings_test.cc
  core/shortcuts_test.cc
  core/tab_strip_layout_test.cc
  core/tab_strip_model_test.cc
  core/totp_test.cc
  core/url_display_test.cc
  core/url_pattern_test.cc
  core/web_origin_test.cc
  core/yaml_io_test.cc
  core/zoom_test.cc
)
brouk_target_options(brouk_core_tests)
target_link_libraries(brouk_core_tests PRIVATE brouk_core_testing GTest::gtest_main)
gtest_discover_tests(brouk_core_tests DISCOVERY_MODE PRE_TEST)
