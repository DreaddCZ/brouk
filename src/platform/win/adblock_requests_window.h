#ifndef BROUK_PLATFORM_WIN_ADBLOCK_REQUESTS_WINDOW_H_
#define BROUK_PLATFORM_WIN_ADBLOCK_REQUESTS_WINDOW_H_

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "cef/adblock_request_handler.h"
#include "platform/win/theme.h"

namespace brouk::win {

// "Show blocked requests" from the shield popup (007), as on macOS: a
// window listing the page's requests that were blocked or allowed by an
// exception, with the request's type, address, rule and list. UI thread.
class AdblockRequestsWindow {
 public:
  // `list_name` gives a list's name for its id; `closed` runs when the user
  // closes the window (the owner deletes it later, not from inside).
  AdblockRequestsWindow(
      HWND owner,
      const std::string& host,
      const std::vector<AdblockRequestInfo>& requests,
      const std::function<std::string(const std::string&)>& list_name,
      std::function<void()> closed);
  ~AdblockRequestsWindow();
  AdblockRequestsWindow(const AdblockRequestsWindow&) = delete;
  AdblockRequestsWindow& operator=(const AdblockRequestsWindow&) = delete;

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);

  HWND window_ = nullptr;
  HWND list_ = nullptr;
  HFONT font_ = nullptr;
  std::function<void()> closed_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_ADBLOCK_REQUESTS_WINDOW_H_
