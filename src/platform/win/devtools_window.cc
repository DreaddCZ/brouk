#include "platform/win/devtools_window.h"

#include <utility>

namespace brouk::win {

DevToolsWindow::DevToolsWindow(HWND near,
                               CefRefPtr<CefBrowser> inspected,
                               const std::wstring& title,
                               std::optional<POINT> inspect,
                               CefRefPtr<CefRequestContext> context,
                               DevToolsPreferences* preferences,
                               Hooks hooks)
    : hooks_(std::move(hooks)), inspected_id_(inspected->GetIdentifier()) {
  frame_ = std::make_unique<DevToolsFrame>(near, [this] { Close(); });
  frame_->SetTitle(L"Developer Tools – " + title);
  dock_ = std::make_unique<DevToolsDock>(frame_->hwnd(), inspected, inspect,
                                         context, preferences,
                                         /*own_tools=*/false, *this);
  dock_->SetPosition("window");
  frame_->Show(dock_->hwnd());
}

DevToolsWindow::~DevToolsWindow() = default;

void DevToolsWindow::InspectAt(POINT point) {
  if (dock_) {
    dock_->InspectAt(point);
  }
  Activate();
}

void DevToolsWindow::Activate() {
  if (frame_ && frame_->hwnd()) {
    if (IsIconic(frame_->hwnd())) {
      ShowWindow(frame_->hwnd(), SW_RESTORE);
    }
    SetForegroundWindow(frame_->hwnd());
  }
}

void DevToolsWindow::Close() {
  if (dock_) {
    dock_->Close();
  }
}

void DevToolsWindow::DockClosed(DevToolsDock& /*dock*/) {
  if (frame_ && frame_->hwnd()) {
    ShowWindow(frame_->hwnd(), SW_HIDE);
  }
  if (hooks_.closed) {
    hooks_.closed(*this);
  }
}

bool DevToolsWindow::DockKey(const CefKeyEvent& event) {
  return hooks_.key && hooks_.key(event);
}

void DevToolsWindow::DockOpenUrl(const std::string& url) {
  if (hooks_.open_url) {
    hooks_.open_url(url);
  }
}

void DevToolsWindow::DockBrowserCount(int delta) {
  if (hooks_.browser_count) {
    hooks_.browser_count(delta);
  }
}

}  // namespace brouk::win
