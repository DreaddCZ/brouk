#include "platform/win/tab.h"

#include <shellapi.h>
#include <shlwapi.h>

#include <cstring>
#include <memory>
#include <utility>

#include "cef/function_task.h"
#include "core/external_protocol.h"
#include "core/permissions.h"
#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"
#include "platform/win/desktop_sources.h"
#include "platform/win/theme.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kContainerClass[] = L"BroukTabContainer";

// Glyphs of the "Segoe MDL2 Assets" icon font.
constexpr wchar_t kWarningGlyph[] = L"\uE7BA";

// The permission prompt's icon for each permission (Segoe MDL2 Assets).
const wchar_t* PermissionGlyph(PermissionKind kind) {
  switch (kind) {
    case PermissionKind::kCamera:
    case PermissionKind::kCameraPanTiltZoom:
      return L"\uE714";
    case PermissionKind::kMicrophone:
      return L"\uE720";
    case PermissionKind::kScreenShare:
      return L"\uE7F4";
    case PermissionKind::kLocation:
      return L"\uE81D";
    case PermissionKind::kNotifications:
      return L"\uEA8F";
    case PermissionKind::kClipboard:
      return L"\uE77F";
    case PermissionKind::kAutomaticDownloads:
      return L"\uE896";
    default:
      return L"\uE785";
  }
}

// The name of the app Windows opens `scheme` links with ("Mail"), or empty
// if none is registered.
std::wstring ExternalAppName(const std::string& scheme) {
  if (scheme.empty()) {
    return {};
  }
  wchar_t name[MAX_PATH] = {};
  DWORD size = MAX_PATH;
  if (FAILED(AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_FRIENDLYAPPNAME,
                               Utf8ToWide(scheme).c_str(), L"open", name,
                               &size))) {
    return {};
  }
  return name;
}

bool RegisterContainerClass(WNDPROC procedure) {
  static const bool registered = [procedure] {
    WNDCLASSEXW container_class = {};
    container_class.cbSize = sizeof(container_class);
    container_class.lpfnWndProc = procedure;
    container_class.hInstance = GetModuleHandleW(nullptr);
    container_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    container_class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    container_class.lpszClassName = kContainerClass;
    return RegisterClassExW(&container_class) != 0;
  }();
  return registered;
}

}  // namespace

Tab::Tab(TabHost& host, TabId id, HWND parent, ProfileId profile)
    : host_(host), id_(id), profile_(std::move(profile)) {
  client_ = new TabClient(this);
  if (RegisterContainerClass(&Tab::ContainerProc)) {
    container_ = CreateWindowExW(0, kContainerClass, L"",
                                 WS_CHILD | WS_CLIPCHILDREN, 0, 0, 0, 0, parent,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
  }
  if (container_) {
    SetWindowLongPtrW(container_, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(this));
  }
}

Tab::~Tab() {
  // Normally detached when the browser closed; a browser that never came
  // must not call back into a deleted tab.
  if (client_) {
    client_->DetachDelegate();
  }
  if (internal_client_) {
    internal_client_->DetachDelegate();
  }
  // The panels' windows go before the container they are in.
  for (auto* panel : {&js_dialog_, &unresponsive_, &external_protocol_,
                      &screen_picker_, &sad_tab_, &permission_prompt_}) {
    panel->reset();
  }
  find_bar_.reset();
  if (container_) {
    SetWindowLongPtrW(container_, GWLP_USERDATA, 0);
    DestroyWindow(container_);
  }
}

// static
LRESULT CALLBACK Tab::ContainerProc(HWND window,
                                    UINT message,
                                    WPARAM wparam,
                                    LPARAM lparam) {
  if (message == WM_SIZE) {
    if (auto* tab =
            reinterpret_cast<Tab*>(GetWindowLongPtrW(window, GWLP_USERDATA))) {
      tab->OnContainerSize();
    }
    return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

void Tab::CreateBrowser(const std::string& url,
                        CefRefPtr<CefRequestContext> context) {
  CEF_REQUIRE_UI_THREAD();
  PopupHost host = PopupHostForBrowser();
  CefWindowInfo info;
  info.SetAsChild(host.parent_view, host.bounds);
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  url_ = url;
  CefBrowserHost::CreateBrowser(info, client_, url, settings, nullptr, context);
}

void Tab::CreateInternalPage(
    const std::string& url,
    std::unique_ptr<CefMessageRouterBrowserSide::Handler> handler,
    CefRefPtr<CefRequestContext> context) {
  CEF_REQUIRE_UI_THREAD();
  // The web client is not used for this tab.
  client_->DetachDelegate();
  client_ = nullptr;
  internal_client_ = new InternalPageClient(this, std::move(handler));
  RECT bounds = {};
  GetClientRect(container_, &bounds);
  CefWindowInfo info;
  info.SetAsChild(container_, CefRect(0, 0, bounds.right, bounds.bottom));
  info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  CefBrowserSettings settings;
  url_ = url;
  CefBrowserHost::CreateBrowser(info, internal_client_, url, settings, nullptr,
                                context);
}

std::string Tab::DisplayTitle() const {
  if (url_.empty() || url_ == "about:blank") {
    return "New Tab";
  }
  return title_.empty() ? url_ : title_;
}

PopupHost Tab::PopupHostForBrowser() {
  RECT bounds = {};
  GetClientRect(container_, &bounds);
  PopupHost host;
  host.parent_view = container_;
  host.bounds = CefRect(0, 0, bounds.right, bounds.bottom);
  host.client = client_;
  return host;
}

HWND Tab::BrowserWindowHandle() const {
  return browser_ ? browser_->GetHost()->GetWindowHandle() : nullptr;
}

bool Tab::IsPageWindow(HWND window) const {
  HWND browser = BrowserWindowHandle();
  return browser && (window == browser || IsChild(browser, window));
}

void Tab::OnContainerSize() {
  // The browser's window fills its tab's container; the panels keep their
  // place over it.
  RECT bounds = {};
  GetClientRect(container_, &bounds);
  if (HWND browser = BrowserWindowHandle()) {
    SetWindowPos(browser, nullptr, 0, 0, bounds.right, bounds.bottom,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
  for (TabPanel* panel :
       {js_dialog_.get(), unresponsive_.get(), external_protocol_.get(),
        screen_picker_.get(), sad_tab_.get(), permission_prompt_.get()}) {
    if (panel) {
      panel->Place();
    }
  }
  if (find_bar_) {
    find_bar_->Place();
  }
}

bool Tab::ForwardShortcut(int key, bool control, bool alt, bool shift) {
  CefKeyEvent event;
  event.type = KEYEVENT_RAWKEYDOWN;
  event.windows_key_code = key;
  event.modifiers = (control ? EVENTFLAG_CONTROL_DOWN : 0) |
                    (alt ? EVENTFLAG_ALT_DOWN : 0) |
                    (shift ? EVENTFLAG_SHIFT_DOWN : 0);
  return host_.OnTabKey(event);
}

std::unique_ptr<TabPanel> Tab::MakePanel(TabPanel::Content content,
                                         TabPanel::Done done) {
  // Browser shortcuts work while a panel has the focus, as in the page.
  auto shortcuts = [this](int key, bool control, bool alt, bool shift) {
    return ForwardShortcut(key, control, alt, shift);
  };
  return std::make_unique<TabPanel>(container_, CurrentTheme(),
                                    std::move(content), std::move(done),
                                    shortcuts);
}

TabPanel* Tab::ModalPanel() const {
  for (TabPanel* panel :
       {unresponsive_.get(), js_dialog_.get(), external_protocol_.get(),
        screen_picker_.get(), move_question_.get()}) {
    if (panel && !panel->answered()) {
      return panel;
    }
  }
  return nullptr;
}

bool Tab::FocusInTab() const {
  HWND focus = GetFocus();
  return !focus || focus == container_ || IsChild(container_, focus);
}

void Tab::UpdatePanels(bool refocus) {
  // A disabled window and its children get no mouse or keyboard input.
  if (HWND browser = BrowserWindowHandle()) {
    EnableWindow(browser, ModalPanel() == nullptr);
  }
  // A hidden tab's dialog waits for the user to switch to it, as in Chrome.
  if (refocus && IsWindowVisible(container_)) {
    Focus();
  }
}

void Tab::ClosePanel(std::unique_ptr<TabPanel>& panel) {
  if (!panel) {
    return;
  }
  const bool refocus = FocusInTab();
  panel.reset();
  UpdatePanels(refocus);
}

void Tab::SetBounds(const RECT& bounds) {
  SetWindowPos(container_, nullptr, bounds.left, bounds.top,
               bounds.right - bounds.left, bounds.bottom - bounds.top,
               SWP_NOZORDER | SWP_NOACTIVATE);
}

void Tab::SetVisible(bool visible) {
  // Chromium notices a hidden window and stops rendering the tab (CEF's
  // WasHidden is only for windowless browsers).
  ShowWindow(container_, visible ? SW_SHOWNA : SW_HIDE);
}

void Tab::UpdateFaviconSize() {
  favicon_ = Favicon::FromPng(favicon_png_, host_.FaviconSize());
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
  if (TabPanel* panel = ModalPanel()) {
    panel->Focus();
  } else if (sad_tab_ && !sad_tab_->answered()) {
    sad_tab_->Focus();
  } else if (browser_) {
    browser_->GetHost()->SetFocus(true);
  }
}

void Tab::ShowFindBar() {
  if (!find_bar_) {
    find_bar_ = std::make_unique<FindBar>(container_, CurrentTheme(), *this);
  }
  find_bar_->Focus();
}

void Tab::FindNext(bool forward) {
  if (!find_bar_ || find_bar_->text().empty()) {
    ShowFindBar();
    return;
  }
  if (browser_) {
    finding_from_bar_ = find_bar_->HasFocus();
    browser_->GetHost()->Find(WideToUtf8(find_bar_->text()), forward,
                              /*matchCase=*/false, /*findNext=*/true);
  }
}

void Tab::OnFindTextChanged() {
  const std::string text = find_bar_ ? WideToUtf8(find_bar_->text()) : "";
  if (!browser_) {
    return;
  }
  if (text.empty()) {
    browser_->GetHost()->StopFinding(/*clearSelection=*/true);
    find_bar_->SetResult(0, 0);
    return;
  }
  finding_from_bar_ = find_bar_->HasFocus();
  // As Chrome's find bar: a new text starts a new search (Chromium sees
  // the text changed) and makes the first match the current one ("1/6");
  // without it all matches are only highlighted ("0/6").
  browser_->GetHost()->Find(text, /*forward=*/true, /*matchCase=*/false,
                            /*findNext=*/true);
}

void Tab::OnFindNext(bool forward) {
  FindNext(forward);
}

void Tab::OnFindClose() {
  // Not from inside the bar's own window procedure, which calls this.
  std::weak_ptr<int> alive = alive_;
  CefPostTask(TID_UI, new FunctionTask([this, alive] {
                if (alive.expired() || !find_bar_) {
                  return;
                }
                const bool refocus = FocusInTab();
                find_bar_.reset();
                if (browser_) {
                  browser_->GetHost()->StopFinding(/*clearSelection=*/true);
                }
                if (refocus && IsWindowVisible(container_)) {
                  Focus();
                }
              }));
}

bool Tab::OnFindShortcut(int key, bool control, bool alt, bool shift) {
  return ForwardShortcut(key, control, alt, shift);
}

void Tab::Zoom(cef_zoom_command_t command) {
  if (browser_ && browser_->GetHost()->CanZoom(command)) {
    browser_->GetHost()->Zoom(command);
    host_.OnTabChanged(*this);
  }
}

double Tab::ZoomLevel() const {
  return browser_ ? browser_->GetHost()->GetZoomLevel() : 0.0;
}

void Tab::Print() {
  if (browser_) {
    browser_->GetHost()->Print();
  }
}

void Tab::ExitFullscreen() {
  if (browser_ && browser_->GetHost()->IsFullscreen()) {
    browser_->GetHost()->ExitFullscreen(/*will_cause_resize=*/true);
  }
}

void Tab::SetMuted(bool muted) {
  muted_ = muted;
  if (browser_) {
    browser_->GetHost()->SetAudioMuted(muted);
  }
  host_.OnTabChanged(*this);
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
  // A popup's browser is created by CEF in the bounds it was given then.
  RECT bounds = {};
  GetClientRect(container_, &bounds);
  SetWindowPos(BrowserWindowHandle(), nullptr, 0, 0, bounds.right,
               bounds.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
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
  title_ = title;
  host_.OnTabChanged(*this);
}

void Tab::OnAddressChanged(const std::string& url) {
  // Like Chrome, the list of blocked popups belongs to the page shown.
  auto without_fragment = [](const std::string& address) {
    return address.substr(0, address.find('#'));
  };
  if (without_fragment(url) != without_fragment(url_)) {
    blocked_popups_.clear();
  }
  url_ = url;
  host_.OnTabChanged(*this);
}

void Tab::OnPageVisited(const std::string& /*url*/,
                        const std::string& /*title*/,
                        cef_transition_type_t /*transition*/) {
  // History (013) comes with W2.
}

void Tab::OnLoadingStateChanged(bool is_loading,
                                bool can_go_back,
                                bool can_go_forward) {
  if (is_loading && sad_tab_) {
    // A new load (Reload) replaces the crash page.
    sad_tab_.reset();
  }
  if (is_loading && !loading_ && js_dialog_ && !js_dialog_->answered() &&
      js_dialog_while_idle_) {
    // A navigation (the address bar, back) cancels the page's dialog, as
    // in Chrome; the page waits in it and would hold the navigation up. A
    // "leave site?" dialog comes while loading and stays.
    js_dialog_->Cancel();
  }
  loading_ = is_loading;
  can_go_back_ = can_go_back;
  can_go_forward_ = can_go_forward;
  host_.OnTabChanged(*this);
}

void Tab::OnFaviconChanged(const std::string& /*icon_url*/,
                           const std::vector<std::uint8_t>& png) {
  favicon_png_ = png;
  UpdateFaviconSize();
  host_.OnTabChanged(*this);
}

std::optional<PopupHost> Tab::OnBeforePopup(const PopupRequest& request) {
  return host_.OnTabPopup(*this, request);
}

void Tab::OnPopupBlocked(const std::string& target_url) {
  constexpr size_t kMaxBlockedPopups = 20;
  if (blocked_popups_.size() < kMaxBlockedPopups) {
    blocked_popups_.push_back(target_url);
  }
  host_.OnTabChanged(*this);
}

void Tab::ClearBlockedPopups() {
  blocked_popups_.clear();
  host_.OnTabChanged(*this);
}

void Tab::OnPopupAborted(int popup_id) {
  host_.OnTabPopupAborted(*this, popup_id);
}

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
  if (internal_client_) {
    internal_client_->DetachDelegate();
    internal_client_ = nullptr;
  }
  browser_ = nullptr;
  closing_ = true;
  // May delete this tab later; nothing here touches it afterwards.
  host_.OnTabClosed(*this);
}

void Tab::ShowJsDialog(const JsDialogRequest& request, JsDialogDone done) {
  using Kind = JsDialogRequest::Kind;
  TabPanel::Content content;
  if (request.kind == Kind::kBeforeUnload) {
    // Like Chrome, the page's own text is not shown (it could mislead).
    content.title = request.is_reload ? L"Reload site?" : L"Leave site?";
    content.message = L"Changes you made may not be saved.";
    content.buttons = {request.is_reload ? L"Reload" : L"Leave", L"Cancel"};
  } else {
    content.title = request.origin.empty()
                        ? std::wstring(L"This page says")
                        : Utf8ToWide(request.origin) + L" says";
    content.message = Utf8ToWide(request.message);
    content.buttons = {L"OK"};
    if (request.kind != Kind::kAlert) {
      content.buttons.emplace_back(L"Cancel");
    }
  }
  if (request.kind == Kind::kPrompt) {
    content.input = Utf8ToWide(request.default_prompt_text);
    content.input_name = L"Your answer";
  }
  if (request.offer_suppression) {
    content.checkbox = L"Prevent this page from creating additional dialogs";
  }
  // Escape cancels; an alert has only OK.
  content.escape_answer = request.kind == Kind::kAlert ? 0 : 1;
  std::weak_ptr<int> alive = alive_;
  js_dialog_while_idle_ = !loading_;
  js_dialog_ = MakePanel(
      std::move(content),
      [this, alive, done = std::move(done)](const TabPanel::Answer& answer) {
        if (!alive.expired() && js_dialog_ && js_dialog_->answered()) {
          ClosePanel(js_dialog_);
        }
        done(answer.button == 0, WideToUtf8(answer.input), answer.checked);
      });
  UpdatePanels(true);
}

void Tab::DismissJsDialog() {
  ClosePanel(js_dialog_);
}

void Tab::OnRendererGone(RendererGoneReason reason,
                         const std::string& error_code) {
  ClosePanel(js_dialog_);
  ClosePanel(unresponsive_);
  TabPanel::Content content;
  content.placement = TabPanel::Placement::kFill;
  content.width = 480;
  content.icon = kWarningGlyph;
  content.title = L"Aw, Snap!";
  content.message =
      reason == RendererGoneReason::kOutOfMemory
          ? L"The page ran out of memory while displaying this webpage."
          : L"Something went wrong while displaying this webpage.";
  content.detail = Utf8ToWide(error_code);
  content.buttons = {L"Reload"};
  std::weak_ptr<int> alive = alive_;
  const bool refocus = FocusInTab();
  sad_tab_ = MakePanel(std::move(content),
                       [this, alive](const TabPanel::Answer& /*answer*/) {
                         if (!alive.expired()) {
                           Reload();
                         }
                       });
  if (refocus && IsWindowVisible(container_)) {
    sad_tab_->Focus();
  }
  loading_ = false;
  host_.OnTabChanged(*this);
}

// Chrome's "Page unresponsive" question, over the tab.
void Tab::OnRendererUnresponsive(UnresponsiveDone done) {
  const std::wstring name = Utf8ToWide(title_.empty() ? url_ : title_);
  TabPanel::Content content;
  content.title = L"Page unresponsive";
  content.message = L"\u201C" + name +
                    L"\u201D is not responding. You can wait for it to "
                    L"become responsive or exit the page.";
  content.buttons = {L"Wait", L"Exit page"};
  content.escape_answer = 0;
  std::weak_ptr<int> alive = alive_;
  unresponsive_ = MakePanel(
      std::move(content),
      [this, alive, done = std::move(done)](const TabPanel::Answer& answer) {
        if (!alive.expired() && unresponsive_ && unresponsive_->answered()) {
          ClosePanel(unresponsive_);
        }
        done(/*exit_page=*/answer.button == 1);
      });
  UpdatePanels(true);
}

void Tab::OnRendererResponsive() {
  ClosePanel(unresponsive_);
}

void Tab::ShowPermissionPrompt(const PermissionRequest& request,
                               PermissionDone done) {
  permission_requests_.push_back({request, std::move(done)});
  if (!permission_prompt_) {
    ShowNextPermissionPrompt();
  }
}

// The prompt at the top left of the page, under the address bar, as in
// Chrome (012). It does not block the page.
void Tab::ShowNextPermissionPrompt() {
  permission_prompt_.reset();
  if (permission_requests_.empty()) {
    return;
  }
  const PermissionRequest& request = permission_requests_.front().request;
  TabPanel::Content content;
  content.placement = TabPanel::Placement::kTopLeft;
  content.width = 340;
  content.title = Utf8ToWide(PermissionPromptTitle(request.origin));
  content.close_button = true;
  for (PermissionKind kind : request.kinds) {
    content.lines.push_back(
        {PermissionGlyph(kind), Utf8ToWide(PermissionDescription(kind))});
  }
  std::vector<PermissionChoice> choices;
  for (const PermissionButton& button : PermissionButtons(request.kinds)) {
    content.buttons.push_back(Utf8ToWide(button.title));
    choices.push_back(button.choice);
  }
  content.first_is_default = false;
  content.stacked_buttons = true;
  content.escape_answer = TabPanel::kClosed;
  std::weak_ptr<int> alive = alive_;
  const std::uint64_t id = request.id;
  permission_prompt_ = MakePanel(
      std::move(content),
      [this, alive, id, choices](const TabPanel::Answer& answer) {
        if (alive.expired() || permission_requests_.empty() ||
            permission_requests_.front().request.id != id) {
          return;  // The tab closed, or the request went away meanwhile.
        }
        PermissionDone done = std::move(permission_requests_.front().done);
        permission_requests_.pop_front();
        const bool refocus = FocusInTab();
        ShowNextPermissionPrompt();
        UpdatePanels(refocus);
        const auto index = static_cast<size_t>(answer.button);
        done(answer.button >= 0 && index < choices.size()
                 ? choices[index]
                 : PermissionChoice::kDismiss);
      });
}

void Tab::DismissPermissionPrompt(std::uint64_t id) {
  if (!permission_requests_.empty() &&
      permission_requests_.front().request.id == id) {
    const bool refocus = FocusInTab();
    permission_requests_.pop_front();
    ShowNextPermissionPrompt();
    UpdatePanels(refocus);
    return;
  }
  std::erase_if(permission_requests_, [id](const PendingPermission& pending) {
    return pending.request.id == id;
  });
}

// Chrome's "Open <app>?" question. Links to schemes no app handles do
// nothing, as in Chrome.
void Tab::AskOpenExternalProtocol(const ExternalProtocolRequest& request,
                                  ExternalProtocolDone done) {
  const std::wstring app = ExternalAppName(request.scheme);
  if (app.empty()) {
    done(false, false);
    return;
  }
  const std::wstring origin = Utf8ToWide(request.origin);
  TabPanel::Content content;
  content.title = L"Open " + app + L"?";
  content.message =
      origin.empty() ? std::wstring(L"A link wants to open this application.")
                     : origin + L" wants to open this application.";
  if (request.can_remember) {
    content.checkbox = L"Always allow " + origin + L" to open " +
                       Utf8ToWide(request.scheme) +
                       L": links in the associated app";
  }
  content.buttons = {L"Open " + app, L"Cancel"};
  content.escape_answer = 1;
  std::weak_ptr<int> alive = alive_;
  external_protocol_ = MakePanel(
      std::move(content),
      [this, alive, done = std::move(done)](const TabPanel::Answer& answer) {
        if (!alive.expired() && external_protocol_ &&
            external_protocol_->answered()) {
          ClosePanel(external_protocol_);
        }
        const bool open = answer.button == 0;
        done(open, open && answer.checked);
      });
  UpdatePanels(true);
}

void Tab::SendRendererConfig(bool json_viewer) {
  if (browser_ && client_) {
    TabClient::SendRendererConfig(browser_, json_viewer);
  }
}

void Tab::AskMoveToProfile(const std::wstring& profile_name,
                           std::function<void(bool move)> done) {
  if (move_question_ || closing_) {
    return;  // Already asked; the first answer counts.
  }
  TabPanel::Content content;
  content.title = L"Move this tab to \u201C" + profile_name + L"\u201D?";
  content.message = L"The page reloads in the " + profile_name +
                    L" profile. You may need to sign in again there, and "
                    L"anything typed into the page is lost.";
  content.buttons = {L"Move", L"Cancel"};
  content.escape_answer = 1;
  std::weak_ptr<int> alive = alive_;
  move_question_ = MakePanel(
      std::move(content),
      [this, alive, done = std::move(done)](const TabPanel::Answer& answer) {
        if (!alive.expired() && move_question_ && move_question_->answered()) {
          ClosePanel(move_question_);
        }
        done(answer.button == 0);
      });
  UpdatePanels(true);
}

// Opens the link with the app Windows has for it. The URL comes from a
// page: it is escaped for the shell, and only schemes with a registered
// app are handed over (never a drive letter or a file).
void Tab::OpenExternalUrl(const std::string& url) {
  const std::optional<std::string> shell_url = ExternalUrlForShell(url);
  if (!shell_url || ExternalAppName(SchemeOf(url)).empty()) {
    return;
  }
  // A URL for its registered app, not a program or a command line (as
  // NSWorkspace openURL on macOS).
  ShellExecuteW(  // nosemgrep: cpp-process-spawn
      nullptr, L"open", Utf8ToWide(*shell_url).c_str(), nullptr, nullptr,
      SW_SHOWNORMAL);
}

void Tab::OnFullscreenModeChanged(bool fullscreen) {
  host_.OnTabFullscreenChanged(*this, fullscreen);
}

void Tab::OnAudibleChanged(bool /*audible*/) {}

void Tab::OnMediaAccessChanged(bool /*video*/, bool /*audio*/) {}

// Windows has no system picker that names Chromium's sources, so Brouk
// lists the screens and windows itself (Chrome's picker, without the
// thumbnails).
void Tab::ChooseDesktopSource(DesktopSourceDone done) {
  // Only the tab the user is looking at may open the picker.
  if ((screen_picker_ && !screen_picker_->answered()) ||
      !IsWindowVisible(container_)) {
    done(std::nullopt);
    return;
  }
  constexpr size_t kMaxWindows = 12;
  const std::vector<DesktopSourceOption> options =
      ListDesktopSources(kMaxWindows);
  TabPanel::Content content;
  content.title = L"Choose what to share";
  content.message =
      L"The site will be able to see the contents of your "
      L"screen or of the window you choose.";
  std::vector<DesktopSource> sources;
  for (const DesktopSourceOption& option : options) {
    content.buttons.push_back(option.title);
    sources.push_back(option.source);
  }
  content.buttons.emplace_back(L"Cancel");
  content.first_is_default = false;
  content.stacked_buttons = true;
  content.escape_answer = static_cast<int>(sources.size());
  std::weak_ptr<int> alive = alive_;
  screen_picker_ = MakePanel(
      std::move(content), [this, alive, sources, done = std::move(done)](
                              const TabPanel::Answer& answer) {
        if (!alive.expired() && screen_picker_ && screen_picker_->answered()) {
          ClosePanel(screen_picker_);
        }
        const auto index = static_cast<size_t>(answer.button);
        done(answer.button >= 0 && index < sources.size()
                 ? std::optional<DesktopSource>(sources[index])
                 : std::nullopt);
      });
  UpdatePanels(true);
}

void Tab::OnFindResult(int count, int active, bool final_update) {
  if (!find_bar_) {
    return;
  }
  find_bar_->SetResult(active, count);
  if (finding_from_bar_ && IsPageWindow(GetFocus())) {
    find_bar_->Refocus();
  }
  if (final_update) {
    finding_from_bar_ = false;
  }
}

void Tab::OpenUrl(const std::string& url, OpenDisposition disposition) {
  host_.OpenUrlFromTab(*this, url, disposition);
}

bool Tab::RouteFirstNavigation(const std::string& /*url*/) {
  return false;  // Link routing (010) comes with W3.
}

void Tab::CopyImageToClipboard(const std::vector<std::uint8_t>& /*png*/) {}

TabClientDelegate::SearchEngine Tab::CurrentSearchEngine() {
  return host_.TabSearchEngine();
}

void Tab::InspectElement(int /*x*/, int /*y*/) {}

void Tab::OnInspectElement(int /*x*/, int /*y*/) {}  // Dev tools (006): W3.

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
