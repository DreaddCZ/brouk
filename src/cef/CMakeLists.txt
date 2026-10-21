# CEF integration: portable C++ (builds on Linux too), no platform UI code.
add_library(brouk_cef STATIC
  browser_app.cc
  browser_app.h
  tab_client.cc
  tab_client.h
)
brouk_target_options(brouk_cef CEF APP)
target_link_libraries(brouk_cef PUBLIC brouk_core libcef_dll_wrapper)
target_include_directories(brouk_cef SYSTEM PUBLIC "${CEF_ROOT}")
