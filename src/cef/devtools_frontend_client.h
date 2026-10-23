#ifndef BROUK_CEF_DEVTOOLS_FRONTEND_CLIENT_H_
#define BROUK_CEF_DEVTOOLS_FRONTEND_CLIENT_H_

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "cef/devtools_preferences.h"
#include "include/cef_client.h"
#include "include/cef_devtools_message_observer.h"
#include "include/cef_registration.h"

namespace brouk {

// Platform side of a tab's docked DevTools (006). UI thread.
class DevToolsFrontendDelegate {
 public:
  virtual ~DevToolsFrontendDelegate() = default;
  virtual void OnDevToolsCreated(CefRefPtr<CefBrowser> frontend) = 0;
  // Remove the DevTools view to complete closing (as for tabs).
  virtual void OnDevToolsCloseReady() = 0;
  virtual void OnDevToolsClosed() = 0;
  // Keys DevTools did not use (Cmd+Opt+I closes the dock from inside it).
  virtual bool OnDevToolsKeyEvent(const CefKeyEvent& event,
                                  CefEventHandle os_event) = 0;
  // The frontend asked to close (its close button).
  virtual void OnDevToolsWantsClose() = 0;
  // "Open in new tab" in DevTools (http and https only).
  virtual void OpenUrlFromDevTools(const std::string& url) = 0;
  virtual void CopyTextFromDevTools(const std::string& text) = 0;
};

// The client of the browser that shows Chromium's DevTools frontend in a
// tab's dock, and the frontend's embedder (006). CEF shows the stock
// DevTools only in its own window on macOS, so Brouk embeds the frontend
// itself, like Chrome's DevToolsUIBindings and Content Shell's
// ShellDevToolsBindings: it answers the frontend's embedder messages and
// passes Chrome DevTools Protocol messages between the frontend and the
// inspected tab (CefBrowserHost::SendDevToolsMessage and a message
// observer). Only the frontend page (devtools://devtools/bundled/) may send
// embedder messages. UI thread.
class DevToolsFrontendClient : public CefClient,
                               public CefKeyboardHandler,
                               public CefLifeSpanHandler,
                               public CefRequestHandler,
                               public CefDevToolsMessageObserver {
 public:
  DevToolsFrontendClient(DevToolsFrontendDelegate* delegate,
                         CefRefPtr<CefBrowser> inspected,
                         DevToolsPreferences* preferences);
  DevToolsFrontendClient(const DevToolsFrontendClient&) = delete;
  DevToolsFrontendClient& operator=(const DevToolsFrontendClient&) = delete;

  // The tab is going away: no more calls to it, no more page messages.
  void DetachDelegate();

  // "Inspect": selects the element at (x, y) of the page in Elements.
  void InspectAt(int x, int y);
  // Shows a panel ("console", "elements"), or starts picking an element.
  void ShowPanel(const std::string& panel);
  void EnterInspectElementMode();
  // Undoes what DevTools changed on the page (paused debugger, emulation,
  // throttling, highlights) and closes the frontend browser.
  void Close();

  // CefClient:
  CefRefPtr<CefKeyboardHandler> GetKeyboardHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                CefRefPtr<CefFrame> frame,
                                CefProcessId source_process,
                                CefRefPtr<CefProcessMessage> message) override;

  // CefKeyboardHandler:
  bool OnKeyEvent(CefRefPtr<CefBrowser> browser,
                  const CefKeyEvent& event,
                  CefEventHandle os_event) override;

  // CefLifeSpanHandler:
  bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     int popup_id,
                     const CefString& target_url,
                     const CefString& target_frame_name,
                     WindowOpenDisposition target_disposition,
                     bool user_gesture,
                     const CefPopupFeatures& popup_features,
                     CefWindowInfo& window_info,
                     CefRefPtr<CefClient>& client,
                     CefBrowserSettings& settings,
                     CefRefPtr<CefDictionaryValue>& extra_info,
                     bool* no_javascript_access) override;
  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  bool DoClose(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  // CefRequestHandler:
  bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                      CefRefPtr<CefFrame> frame,
                      CefRefPtr<CefRequest> request,
                      bool user_gesture,
                      bool is_redirect) override;

  // CefDevToolsMessageObserver (messages from the inspected tab):
  bool OnDevToolsMessage(CefRefPtr<CefBrowser> browser,
                         const void* message,
                         size_t message_size) override;

 private:
  using PageResult = std::function<void(CefRefPtr<CefDictionaryValue>)>;

  void HandleEmbedderMessage(const std::string& json);
  void Ack(int id, const CefRefPtr<CefValue>& result);
  // Calls `function` (e.g. "DevToolsAPI.dispatchMessage") in the frontend.
  void CallFrontend(std::string_view function,
                    const std::vector<CefRefPtr<CefValue>>& args);
  void ForwardToFrontend(std::string_view message);
  // A protocol command of our own (ids above the frontend's); its result
  // goes to `done` and never to the frontend.
  void SendToPage(const std::string& method,
                  const CefRefPtr<CefDictionaryValue>& params,
                  PageResult done);
  void WhenLoaded(std::function<void()> action);
  void ResetPage();

  DevToolsFrontendDelegate* delegate_;  // Not owned; null when detached.
  CefRefPtr<CefBrowser> inspected_;
  CefRefPtr<CefBrowser> frontend_;
  CefRefPtr<CefRegistration> registration_;
  DevToolsPreferences* preferences_;  // Not owned; outlives the client.
  bool loaded_ = false;
  std::vector<std::function<void()>> after_load_;
  // Ids of our own protocol commands; the frontend counts up from 1.
  int next_own_id_ = 1'000'000'000;
  std::map<int, PageResult> own_requests_;

  IMPLEMENT_REFCOUNTING(DevToolsFrontendClient);
};

}  // namespace brouk

#endif  // BROUK_CEF_DEVTOOLS_FRONTEND_CLIENT_H_
