#ifndef BROUK_CEF_DOWNLOADS_PAGE_HANDLER_H_
#define BROUK_CEF_DOWNLOADS_PAGE_HANDLER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cef/download_service.h"
#include "core/profile_registry.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// What the downloads page (brouk://downloads, 014) needs from the browser;
// one per page tab. UI thread.
class DownloadsPageDelegate {
 public:
  virtual ~DownloadsPageDelegate() = default;
  virtual std::vector<ProfileInfo> Profiles() = 0;
  virtual ProfileId CurrentProfile() = 0;
  virtual DownloadService* Service() = 0;
  virtual bool FileExists(const std::string& path) = 0;
  virtual void OpenFile(const std::string& path) = 0;
  virtual void RevealFile(const std::string& path) = 0;
  // Moves the file to the Trash.
  virtual bool TrashFile(const std::string& path) = 0;
  virtual void CopyText(const std::string& text) = 0;
  virtual void Retry(const DownloadEntry& entry) = 0;
  // Calls `changed` whenever the downloads change, until the page goes.
  virtual void Watch(std::function<void()> changed) = 0;
};

// Answers the downloads page's bridge messages (web/downloads/api.ts).
// Only brouk://downloads may ask; every id and profile is checked against
// the downloads the browser knows. UI thread.
class DownloadsPageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit DownloadsPageHandler(
      std::unique_ptr<DownloadsPageDelegate> delegate);
  DownloadsPageHandler(const DownloadsPageHandler&) = delete;
  DownloadsPageHandler& operator=(const DownloadsPageHandler&) = delete;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override;
  void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame> frame,
                       int64_t query_id) override;

 private:
  std::shared_ptr<DownloadsPageDelegate> delegate_;
  std::shared_ptr<std::map<int64_t, CefRefPtr<Callback>>> watchers_;
  bool watching_ = false;
};

}  // namespace brouk

#endif  // BROUK_CEF_DOWNLOADS_PAGE_HANDLER_H_
