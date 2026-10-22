// Entry point of the helper processes (renderer, GPU, network, ...) on macOS.

#include "include/cef_app.h"
#include "include/wrapper/cef_library_loader.h"
#include "renderer/renderer_app.h"

#if defined(CEF_USE_SANDBOX)
#include "include/cef_sandbox_mac.h"
#endif

int main(int argc, char* argv[]) {
#if defined(CEF_USE_SANDBOX)
  CefScopedSandboxContext sandbox_context;
  if (!sandbox_context.Initialize(argc, argv)) {
    return 1;
  }
#endif

  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInHelper()) {
    return 1;
  }

  CefMainArgs main_args(argc, argv);
  // The same app object serves every helper type; only renderer processes
  // use its handler.
  CefRefPtr<brouk::RendererApp> app(new brouk::RendererApp());
  return CefExecuteProcess(main_args, app, nullptr);
}
