# CEF integration: portable C++ (builds on Linux too), no platform UI code.
add_library(brouk_cef STATIC
  browser_app.cc
  browser_app.h
  context_menu_model.cc
  context_menu_model.h
  internal_page_client.cc
  internal_page_client.h
  internal_scheme.cc
  internal_scheme.h
  permission_mapping.cc
  permission_mapping.h
  profile_prefs.cc
  profile_manager_handler.cc
  profile_manager_handler.h
  profile_prefs.h
  settings_handler.cc
  settings_handler.h
  site_info.cc
  site_info.h
  tab_client.cc
  tab_client.h
)
brouk_target_options(brouk_cef CEF APP)
target_link_libraries(brouk_cef PUBLIC brouk_core libcef_dll_wrapper)
target_include_directories(brouk_cef SYSTEM PUBLIC "${CEF_ROOT}")
