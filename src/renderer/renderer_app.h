#ifndef BROUK_RENDERER_RENDERER_APP_H_
#define BROUK_RENDERER_RENDERER_APP_H_

#include "include/cef_app.h"

namespace brouk {

// CefApp for renderer processes (the helper app). Adds a small script to every
// page that reports what CEF cannot tell the browser itself: popups blocked by
// Chromium's popup blocker and whether the page plays audio (see
// core/page_events.h). Runs on the renderer main thread.
class RendererApp : public CefApp, public CefRenderProcessHandler {
 public:
  RendererApp() = default;
  RendererApp(const RendererApp&) = delete;
  RendererApp& operator=(const RendererApp&) = delete;

  // CefApp:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }

  // CefRenderProcessHandler:
  void OnContextCreated(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefV8Context> context) override;

 private:
  IMPLEMENT_REFCOUNTING(RendererApp);
};

}  // namespace brouk

#endif  // BROUK_RENDERER_RENDERER_APP_H_
