#ifndef BROUK_PLATFORM_WIN_LINK_RESOLVER_H_
#define BROUK_PLATFORM_WIN_LINK_RESOLVER_H_

#include <windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cef/link_resolver_client.h"
#include "core/link_routing_rules.h"
#include "include/cef_browser.h"

namespace brouk::win {

// Resolves opaque link wrappers (010: Mimecast, …), as on macOS: loads the
// link in a neutral, throw-away request context (its own folder under
// web\, deleted afterwards; no profile) without showing it, and reports
// where it leads once it leaves the wrapper hosts. A wrapper that stays on
// its own page for 5 seconds (a warning, a "continue" button, an error) is
// shown in a "Checking link" window for the user. UI thread.
class LinkResolver {
 public:
  // `browser_count` learns about the neutral browsers (+1 / -1), so
  // quitting waits for them.
  explicit LinkResolver(std::function<void(int delta)> browser_count);
  ~LinkResolver();
  LinkResolver(const LinkResolver&) = delete;
  LinkResolver& operator=(const LinkResolver&) = delete;

  // `done` gets the real URL, or nullopt when the user closed the
  // "Checking link" window. Each call is resolved on its own.
  using Done = std::function<void(std::optional<std::string> final_url)>;
  void Resolve(const std::string& url,
               const std::string& wrapper_name,
               std::vector<LinkWrapperRule> user_wrappers,
               HMONITOR monitor,
               Done done);
  // Quitting: stops every resolution (their `done` gets nullopt).
  void CancelAll();
  // Folders of resolutions a crash left behind; before CEF starts.
  static void RemoveLeftoverData(const std::filesystem::path& web_data_root);

 private:
  class Resolution;

  std::function<void(int)> browser_count_;
  std::vector<std::unique_ptr<Resolution>> resolutions_;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_LINK_RESOLVER_H_
