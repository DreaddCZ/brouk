#ifndef BROUK_RENDERER_RENDERER_APP_H_
#define BROUK_RENDERER_RENDERER_APP_H_

#include <map>
#include <string>

#include "include/cef_app.h"
#include "include/cef_v8.h"

namespace brouk {

// CefApp for renderer processes (the helper app). Adds a small script to every
// page that reports what CEF cannot tell the browser itself: popups blocked by
// Chromium's popup blocker and whether the page plays audio (see
// core/page_events.h). The script also replaces getDisplayMedia() with one
// that lets the user pick a screen or window (core/screen_share.h). Runs on
// the renderer main thread.
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
  void OnContextReleased(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         CefRefPtr<CefV8Context> context) override;
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // The page script's function receiving the picker's answer. Kept here, out
  // of the page's reach, so a page cannot fake an answer.
  void SetDisplayMediaHandler(CefRefPtr<CefV8Context> context,
                              CefRefPtr<CefV8Value> function);

 private:
  struct DisplayMediaHandler {
    CefRefPtr<CefV8Context> context;
    CefRefPtr<CefV8Value> function;
  };
  // By frame identifier.
  std::map<std::string, DisplayMediaHandler> display_media_handlers_;

  IMPLEMENT_REFCOUNTING(RendererApp);
};

}  // namespace brouk

#endif  // BROUK_RENDERER_RENDERER_APP_H_
