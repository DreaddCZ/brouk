#ifndef BROUK_RENDERER_RENDERER_APP_H_
#define BROUK_RENDERER_RENDERER_APP_H_

#include <map>
#include <string>

#include "include/cef_app.h"
#include "include/cef_v8.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// CefApp for renderer processes (the helper app). Adds a small script to every
// page that reports what CEF cannot tell the browser itself: popups blocked by
// Chromium's popup blocker and whether the page plays audio (see
// core/page_events.h). The script also replaces getDisplayMedia() with one
// that lets the user pick a screen or window (core/screen_share.h). Runs on
// the renderer main thread.
class RendererApp : public CefApp, public CefRenderProcessHandler {
 public:
  RendererApp();
  RendererApp(const RendererApp&) = delete;
  RendererApp& operator=(const RendererApp&) = delete;

  // CefApp:
  CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
    return this;
  }
  void OnRegisterCustomSchemes(
      CefRawPtr<CefSchemeRegistrar> registrar) override;

  // CefRenderProcessHandler:
  void OnWebKitInitialized() override;
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
  // Cosmetic filtering (007): the page script's receiver for the browser's
  // answers, kept out of the page's reach.
  void SetAdblockReceiver(CefRefPtr<CefV8Context> context,
                          CefRefPtr<CefV8Value> function);
  // Login forms (008): the page script's receiver for fill messages.
  void SetFormsReceiver(CefRefPtr<CefV8Context> context,
                        CefRefPtr<CefV8Value> function);

 private:
  struct DisplayMediaHandler {
    CefRefPtr<CefV8Context> context;
    CefRefPtr<CefV8Value> function;
  };
  // By frame identifier.
  std::map<std::string, DisplayMediaHandler> display_media_handlers_;
  std::map<std::string, DisplayMediaHandler> adblock_receivers_;
  std::map<std::string, DisplayMediaHandler> forms_receivers_;
  void DeliverAdblockAnswer(const CefRefPtr<CefFrame>& frame,
                            const std::string& json,
                            const std::string& script);
  // The bridge of internal pages (window.cefQuery), added to brouk:// pages
  // only.
  CefRefPtr<CefMessageRouterRendererSide> router_;
  // Settings from the browser (kRendererConfigMessage); the same for all
  // tabs, so one value per renderer process.
  bool json_viewer_ = true;
  void ApplyConfig(const CefRefPtr<CefDictionaryValue>& config);

  IMPLEMENT_REFCOUNTING(RendererApp);
};

}  // namespace brouk

#endif  // BROUK_RENDERER_RENDERER_APP_H_
