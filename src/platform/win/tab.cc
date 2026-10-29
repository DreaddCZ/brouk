#include "platform/win/tab.h"

#include <cstring>
#include <memory>
#include <utility>

#include "cef/function_task.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

// The JavaScript dialog as a system message box, until the tab-modal dialog
// of 012 is ported (W1). Returns true for OK / Leave.
bool RunJsDialog(HWND owner,
                 JsDialogRequest::Kind kind,
                 const std::wstring& title,
                 const std::wstring& text) {
  const UINT buttons =
      kind == JsDialogRequest::Kind::kAlert ? MB_OK : MB_OKCANCEL;
  return MessageBoxW(owner, text.c_str(), title.c_str(),
                     buttons | MB_ICONINFORMATION) == IDOK;
}

}  // namespace

Tab::Tab(TabHost& host) : host_(host) {
  client_ = new TabClient(this);
}

Tab::~Tab() {
  // Normally detached when the browser closed; a browser that never came
  // must not call back into a deleted tab.
  if (client_) {
    client_->DetachDelegate();
  }
}

void Tab::CreateBrowser(HWND parent,
                        const RECT& bounds,
                        const std::string& url,
                        CefRefPtr<CefRequestContext> context) {
  CEF_REQUIRE_UI_THREAD();
  bounds_ = bounds;
  CefWindowInfo info;
  info.SetAsChild(parent,
                  CefRect(bounds.left, bounds.top, bounds.right - bounds.left,
                          bounds.bottom - bounds.top));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  CefBrowserHost::CreateBrowser(info, client_, url, settings, nullptr, context);
}

HWND Tab::BrowserWindowHandle() const {
  return browser_ ? browser_->GetHost()->GetWindowHandle() : nullptr;
}

void Tab::SetBounds(const RECT& bounds) {
  bounds_ = bounds;
  if (HWND window = BrowserWindowHandle()) {
    SetWindowPos(window, nullptr, bounds.left, bounds.top,
                 bounds.right - bounds.left, bounds.bottom - bounds.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

void Tab::Navigate(const std::string& url) {
  if (!browser_) {
    pending_url_ = url;
    return;
  }
  browser_->GetMainFrame()->LoadURL(url);
}

void Tab::GoBack() {
  if (browser_) {
    browser_->GoBack();
  }
}

void Tab::GoForward() {
  if (browser_) {
    browser_->GoForward();
  }
}

void Tab::Reload() {
  if (browser_) {
    browser_->Reload();
  }
}

void Tab::Focus() {
  if (browser_) {
    browser_->GetHost()->SetFocus(true);
  }
}

void Tab::Close() {
  closing_ = true;
  if (browser_) {
    browser_->GetHost()->CloseBrowser(false);
  }
  // Without a browser yet, OnBrowserCreated closes it as soon as it exists.
}

void Tab::OnBrowserCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  SetBounds(bounds_);
  if (closing_) {
    browser_->GetHost()->CloseBrowser(false);
    return;
  }
  if (!pending_url_.empty()) {
    browser_->GetMainFrame()->LoadURL(pending_url_);
    pending_url_.clear();
  }
}

void Tab::OnTitleChanged(const std::string& title) {
  host_.OnTabTitleChanged(title);
}

void Tab::OnAddressChanged(const std::string& url) {
  host_.OnTabAddressChanged(url);
}

void Tab::OnPageVisited(const std::string& /*url*/,
                        const std::string& /*title*/,
                        cef_transition_type_t /*transition*/) {
  // History (013) comes with W2.
}

void Tab::OnLoadingStateChanged(bool /*is_loading*/,
                                bool can_go_back,
                                bool can_go_forward) {
  host_.OnTabLoadingStateChanged(can_go_back, can_go_forward);
}

void Tab::OnFaviconChanged(const std::string& /*icon_url*/,
                           const std::vector<std::uint8_t>& /*png*/) {
  // Shown by the tab strip (W1).
}

std::optional<PopupHost> Tab::OnBeforePopup(const PopupRequest& request) {
  // Until the tab strip exists, a link the user clicked opens in a new
  // window, without an opener (sign-in popups need the real popup: W1).
  if (request.user_gesture) {
    host_.OpenInNewWindow(request.target_url);
  }
  return std::nullopt;
}

void Tab::OnPopupBlocked(const std::string& /*target_url*/) {}

void Tab::OnPopupAborted(int /*popup_id*/) {}

void Tab::OnCloseReady() {
  // The browser lives in a child window of ours (TabClient::DoClose returns
  // true): destroying that window completes the close, then OnBrowserClosed.
  if (HWND window = BrowserWindowHandle()) {
    DestroyWindow(window);
  }
}

void Tab::OnBrowserClosed() {
  if (client_) {
    client_->DetachDelegate();
    client_ = nullptr;
  }
  browser_ = nullptr;
  host_.OnTabClosed();
}

void Tab::ShowJsDialog(const JsDialogRequest& request, JsDialogDone done) {
  if (request.kind == JsDialogRequest::Kind::kPrompt) {
    done(false, std::string(), false);  // Needs the tab-modal dialog (W1).
    return;
  }
  std::wstring title;
  std::wstring text;
  if (request.kind == JsDialogRequest::Kind::kBeforeUnload) {
    // Chrome's wording.
    title = request.is_reload ? L"Reload site?" : L"Leave site?";
    text = L"Changes that you made may not be saved.";
  } else {
    title = request.origin.empty() ? std::wstring(L"This page says")
                                   : Utf8ToWide(request.origin) + L" says";
    text = Utf8ToWide(request.message);
  }
  // Shown from a task, not inside CEF's callback: the message box runs its
  // own message loop.
  dialog_done_ = std::move(done);
  HWND owner = GetAncestor(BrowserWindowHandle(), GA_ROOT);
  std::weak_ptr<int> alive = alive_;
  const JsDialogRequest::Kind kind = request.kind;
  CefPostTask(TID_UI, new FunctionTask([this, alive, owner, kind, title, text] {
                if (alive.expired() || !dialog_done_) {
                  return;  // The tab closed or the page dismissed the dialog.
                }
                const bool ok = RunJsDialog(owner, kind, title, text);
                if (alive.expired() || !dialog_done_) {
                  return;
                }
                JsDialogDone done = std::move(dialog_done_);
                dialog_done_ = nullptr;
                done(ok, std::string(), false);
              }));
}

void Tab::DismissJsDialog() {
  dialog_done_ = nullptr;
}

void Tab::OnRendererGone(RendererGoneReason /*reason*/,
                         const std::string& /*error_code*/) {
  // The crash page ("Aw, Snap!") comes with W1; Reload loads the page again.
}

void Tab::OnRendererUnresponsive(UnresponsiveDone done) {
  done(false);  // Keep waiting until the question can be asked (W1).
}

void Tab::OnRendererResponsive() {}

void Tab::ShowPermissionPrompt(const PermissionRequest& /*request*/,
                               PermissionDone done) {
  // Asked again next time, once the prompt is ported (W1).
  done(PermissionChoice::kDismiss);
}

void Tab::DismissPermissionPrompt(std::uint64_t /*id*/) {}

void Tab::AskOpenExternalProtocol(const ExternalProtocolRequest& /*request*/,
                                  ExternalProtocolDone done) {
  done(false, false);  // Not yet (W1).
}

void Tab::OpenExternalUrl(const std::string& /*url*/) {}

void Tab::OnFullscreenModeChanged(bool /*fullscreen*/) {}

void Tab::OnAudibleChanged(bool /*audible*/) {}

void Tab::OnMediaAccessChanged(bool /*video*/, bool /*audio*/) {}

void Tab::ChooseDesktopSource(DesktopSourceDone done) {
  done(std::nullopt);  // The screen picker comes with W1.
}

void Tab::OnFindResult(int /*count*/, int /*active*/, bool /*final_update*/) {}

void Tab::OpenUrl(const std::string& url, OpenDisposition /*disposition*/) {
  host_.OpenInNewWindow(url);
}

bool Tab::RouteFirstNavigation(const std::string& /*url*/) {
  return false;  // Link routing (010) comes with W3.
}

void Tab::CopyImageToClipboard(const std::vector<std::uint8_t>& /*png*/) {}

TabClientDelegate::SearchEngine Tab::CurrentSearchEngine() {
  return host_.TabSearchEngine();
}

void Tab::InspectElement(int /*x*/, int /*y*/) {}

bool Tab::AskWhereToSaveDownloads() {
  return false;
}

DownloadTarget Tab::ReserveDownloadPath(const std::string& /*suggested*/) {
  return {std::string(), true};
}

void Tab::ReleaseDownloadPath(const std::string& /*path*/) {}

void Tab::OnAdblockRequest(const AdblockRequestInfo& /*info*/) {}

void Tab::OnPageBlocked(const AdblockRequestInfo& /*info*/) {}

void Tab::OnElementPicked(const std::string& /*url*/,
                          const std::string& /*selector*/) {}

bool Tab::OnUnhandledKeyEvent(const CefKeyEvent& event,
                              CefEventHandle /*os_event*/) {
  return host_.OnTabKey(event);
}

bool Tab::KeepsFocusOnNavigation() {
  return false;
}

void Tab::ShowPasswordMenu(const PasswordMenu& /*menu*/,
                           PasswordMenuDone /*done*/) {}

void Tab::HidePasswordMenu() {}

void Tab::ShowPasswordPrompt(const PasswordPrompt& /*prompt*/,
                             PasswordPromptDone /*done*/) {}

void Tab::OnPasswordIconChanged(PasswordIcon /*icon*/) {}

void Tab::OpenPasswordsPage() {}

void Tab::CopyTextToClipboard(const std::string& text) {
  if (!OpenClipboard(nullptr)) {
    return;
  }
  EmptyClipboard();
  const std::wstring wide = Utf8ToWide(text);
  const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (void* data = GlobalLock(memory)) {
      std::memcpy(data, wide.c_str(), bytes);
      GlobalUnlock(memory);
      if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);  // Owned by the clipboard only on success.
      }
    } else {
      GlobalFree(memory);
    }
  }
  CloseClipboard();
}

bool Tab::HandlePasswordMenuKey(int /*windows_key_code*/) {
  return false;
}

void Tab::DecodeQrCodes(std::vector<std::uint8_t> /*png*/, QrDone done) {
  done({});
}

void Tab::ShowPasswordMessage(const std::string& /*title*/,
                              const std::string& /*text*/) {}

void Tab::AskBitwardenPassword(const BitwardenPasswordRequest& /*request*/,
                               BitwardenPasswordSubmit /*submit*/,
                               std::function<void()> cancelled) {
  cancelled();
}

void Tab::ConfirmWithTouchId(const std::string& /*reason*/, TouchIdDone done) {
  done(TouchIdResult::kUnavailable);  // Windows Hello comes with W3.
}

void Tab::ShowAutofillPrompt(const AutofillPrompt& /*prompt*/,
                             AutofillPromptDone done) {
  done(AutofillPromptAnswer{});  // "Not now".
}

void Tab::OpenAutofillSettings() {}

}  // namespace brouk::win
