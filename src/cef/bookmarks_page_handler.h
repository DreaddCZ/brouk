#ifndef BROUK_CEF_BOOKMARKS_PAGE_HANDLER_H_
#define BROUK_CEF_BOOKMARKS_PAGE_HANDLER_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/bookmark_model.h"
#include "core/profile_id.h"
#include "core/profile_registry.h"
#include "core/result.h"
#include "include/wrapper/cef_message_router.h"

namespace brouk {

// What the bookmark manager page (brouk://bookmarks, 011) needs from the
// browser; one per manager tab. UI thread.
class BookmarksPageDelegate {
 public:
  virtual ~BookmarksPageDelegate() = default;
  virtual std::vector<ProfileInfo> Profiles() = 0;
  // The manager tab's profile (where general bookmarks open).
  virtual ProfileId CurrentProfile() = 0;
  virtual const BookmarkModel& Model() = 0;
  // Changes the app's bookmarks (saved and shown everywhere).
  virtual void Change(const std::function<void(BookmarkModel&)>& change) = 0;
  // Removes items; the last removal can be undone (011).
  virtual void Remove(const std::vector<BookmarkId>& ids) = 0;
  virtual bool Undo() = 0;
  // Opens a bookmark by the opening rules of 011: in the manager tab when
  // it may, or (with `new_tab`) in a new background tab.
  virtual void Open(BookmarkId id, bool new_tab) = 0;
  // Calls `changed` whenever the bookmarks change, until the page goes.
  virtual void Watch(std::function<void()> changed) = 0;
  // Export: asks where to save `html`; `done` gets false when cancelled,
  // or an error. Import: asks for a file; `done` gets its text, nullopt
  // when cancelled, or an error.
  using SaveDone = std::function<void(bool saved, std::optional<Error>)>;
  virtual void SaveHtml(std::string html, SaveDone done) = 0;
  using OpenDone = std::function<void(std::optional<std::string> text,
                                      std::optional<Error>)>;
  virtual void OpenHtml(OpenDone done) = 0;
};

// Answers the manager page's bridge messages (web/bookmarks/api.ts). Every
// message is checked: only brouk://bookmarks may ask, ids must exist,
// names, addresses and profiles are validated by the model. UI thread.
class BookmarksPageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit BookmarksPageHandler(
      std::unique_ptr<BookmarksPageDelegate> delegate);
  BookmarksPageHandler(const BookmarksPageHandler&) = delete;
  BookmarksPageHandler& operator=(const BookmarksPageHandler&) = delete;

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
  // Shared with callbacks that outlive one message (file panels, watching).
  std::shared_ptr<BookmarksPageDelegate> delegate_;
  // The page's "bookmarks.watch" queries, answered on every change.
  std::shared_ptr<std::map<int64_t, CefRefPtr<Callback>>> watchers_;
  bool watching_ = false;
};

}  // namespace brouk

#endif  // BROUK_CEF_BOOKMARKS_PAGE_HANDLER_H_
