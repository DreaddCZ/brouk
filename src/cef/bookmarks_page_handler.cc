#include "cef/bookmarks_page_handler.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cef/json_util.h"
#include "core/address_input.h"
#include "core/bookmark_html.h"
#include "core/internal_pages.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxRequestBytes = std::size_t{256} * 1024;
constexpr std::size_t kMaxIds = 10000;
constexpr std::size_t kMaxImportBytes = std::size_t{64} * 1024 * 1024;
constexpr int kBadRequest = 400;
constexpr int kFailed = 500;
// Ids are JSON numbers; doubles are exact up to 2^53.
constexpr double kMaxId = 9.0e15;

using Callback = CefMessageRouterBrowserSide::Callback;

std::optional<std::string> StringField(
    const CefRefPtr<CefDictionaryValue>& message,
    const char* key,
    std::size_t max_bytes) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > max_bytes) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::int64_t> NumberOf(const CefRefPtr<CefValue>& value) {
  if (value->GetType() == VTYPE_INT) {
    return value->GetInt();
  }
  if (value->GetType() == VTYPE_DOUBLE) {
    double number = value->GetDouble();
    if (number >= 0 && number < kMaxId &&
        number == static_cast<double>(static_cast<std::int64_t>(number))) {
      return static_cast<std::int64_t>(number);
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> NumberField(
    const CefRefPtr<CefDictionaryValue>& message,
    const char* key) {
  if (!message->HasKey(key)) {
    return std::nullopt;
  }
  return NumberOf(message->GetValue(key));
}

// An id of an existing item.
std::optional<BookmarkId> IdField(const CefRefPtr<CefDictionaryValue>& message,
                                  const char* key,
                                  const BookmarkModel& model) {
  std::optional<std::int64_t> id = NumberField(message, key);
  if (!id || model.Find(*id) == nullptr) {
    return std::nullopt;
  }
  return id;
}

std::optional<std::vector<BookmarkId>> IdsField(
    const CefRefPtr<CefDictionaryValue>& message,
    const BookmarkModel& model) {
  if (message->GetType("ids") != VTYPE_LIST) {
    return std::nullopt;
  }
  CefRefPtr<CefListValue> list = message->GetList("ids");
  if (list->GetSize() > kMaxIds) {
    return std::nullopt;
  }
  std::vector<BookmarkId> ids;
  for (std::size_t i = 0; i < list->GetSize(); ++i) {
    std::optional<std::int64_t> id = NumberOf(list->GetValue(i));
    if (!id || model.Find(*id) == nullptr) {
      return std::nullopt;
    }
    ids.push_back(*id);
  }
  return ids;
}

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

// Ids go to the page as doubles: CEF's int is 32 bits.
CefRefPtr<CefDictionaryValue> NodeJson(const BookmarkModel& model,
                                       const BookmarkItem& item) {
  CefRefPtr<CefDictionaryValue> node = CefDictionaryValue::Create();
  node->SetDouble("id", static_cast<double>(item.id));
  node->SetString("type", item.folder ? "folder" : "url");
  node->SetString("name", item.name);
  if (!item.folder) {
    node->SetString("url", item.url);
  }
  node->SetString("profile", item.profile);
  node->SetString("effective", model.EffectiveProfile(item.id));
  node->SetDouble("added", static_cast<double>(item.added));
  if (item.folder) {
    CefRefPtr<CefListValue> children = CefListValue::Create();
    for (BookmarkId child : item.children) {
      if (const BookmarkItem* inner = model.Find(child)) {
        children->SetDictionary(children->GetSize(), NodeJson(model, *inner));
      }
    }
    node->SetList("children", children);
  }
  return node;
}

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// What the user typed as an address, as the address bar reads it
// ("example.com" becomes https://example.com/); nullopt for text that is
// not an address.
std::optional<std::string> NormalizedUrl(const std::string& typed) {
  AddressInputResult result = ClassifyAddressInput(typed);
  if (result.kind != AddressInputResult::Kind::kUrl) {
    return std::nullopt;
  }
  return result.url;
}

bool ProfileExists(BookmarksPageDelegate& delegate, const ProfileId& profile) {
  if (profile.empty()) {
    return true;  // General.
  }
  for (const ProfileInfo& info : delegate.Profiles()) {
    if (info.id == profile) {
      return true;
    }
  }
  return false;
}

}  // namespace

BookmarksPageHandler::BookmarksPageHandler(
    std::unique_ptr<BookmarksPageDelegate> delegate)
    : delegate_(std::move(delegate)),
      watchers_(std::make_shared<std::map<int64_t, CefRefPtr<Callback>>>()) {}

void BookmarksPageHandler::OnQueryCanceled(CefRefPtr<CefBrowser> /*browser*/,
                                           CefRefPtr<CefFrame> /*frame*/,
                                           int64_t query_id) {
  CEF_REQUIRE_UI_THREAD();
  watchers_->erase(query_id);
}

bool BookmarksPageHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                                   CefRefPtr<CefFrame> frame,
                                   int64_t query_id,
                                   const CefString& request,
                                   bool persistent,
                                   CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  // Only the manager page itself (the browser's own view of the frame).
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "bookmarks") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed =
      text.size() <= kMaxRequestBytes ? ParseJson(text) : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::optional<std::string> type =
      message ? StringField(message, "type", 64) : std::nullopt;
  if (!type || persistent != (*type == "bookmarks.watch")) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  BookmarksPageDelegate& delegate = *delegate_;
  const BookmarkModel& model = delegate.Model();
  auto fail = [&callback](const std::string& why) {
    callback->Failure(kBadRequest, why);
    return true;
  };

  if (*type == "bookmarks.watch") {
    watchers_->emplace(query_id, callback);
    if (!watching_) {
      watching_ = true;
      std::weak_ptr<std::map<int64_t, CefRefPtr<Callback>>> weak = watchers_;
      delegate.Watch([weak] {
        if (auto watchers = weak.lock()) {
          for (const auto& [id, watcher] : *watchers) {
            watcher->Success("changed");
          }
        }
      });
    }
    return true;
  }
  if (*type == "bookmarks.state") {
    CefRefPtr<CefListValue> profiles = CefListValue::Create();
    for (const ProfileInfo& info : delegate.Profiles()) {
      CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
      row->SetString("id", info.id);
      row->SetString("name", info.name);
      row->SetString("color", info.color);
      profiles->SetDictionary(profiles->GetSize(), row);
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetList("profiles", profiles);
    answer->SetString("currentProfile", delegate.CurrentProfile());
    answer->SetDictionary("bar",
                          NodeJson(model, *model.Find(BookmarkModel::kBarId)));
    answer->SetDictionary(
        "other", NodeJson(model, *model.Find(BookmarkModel::kOtherId)));
    callback->Success(ToJson(answer));
    return true;
  }
  if (*type == "bookmarks.add") {
    std::optional<BookmarkId> parent = IdField(message, "parent", model);
    std::optional<std::string> name =
        StringField(message, "name", BookmarkModel::kMaxNameBytes);
    std::optional<std::string> profile = StringField(message, "profile", 64);
    bool folder =
        message->GetType("folder") == VTYPE_BOOL && message->GetBool("folder");
    std::optional<std::string> url =
        folder ? std::optional<std::string>("")
               : StringField(message, "url", BookmarkModel::kMaxUrlBytes);
    std::optional<std::int64_t> index = NumberField(message, "index");
    if (!parent || !name || !profile || !url ||
        !ProfileExists(delegate, *profile)) {
      return fail("Invalid request.");
    }
    if (!folder) {
      url = NormalizedUrl(*url);
      if (!url) {
        return fail("Enter a web address, e.g. https://example.com");
      }
    }
    std::size_t at =
        index ? static_cast<std::size_t>(*index) : BookmarkModel::kEnd;
    std::optional<Error> error;
    BookmarkId added = 0;
    delegate.Change([&](BookmarkModel& changed) {
      Result<BookmarkId> id =
          folder
              ? changed.AddFolder(*parent, at, *name, *profile, Now())
              : changed.AddBookmark(*parent, at, *name, *url, *profile, Now());
      if (id) {
        added = id.value();
      } else {
        error = id.error();
      }
    });
    if (error) {
      return fail(error->message);
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetDouble("id", static_cast<double>(added));
    callback->Success(ToJson(answer));
    return true;
  }
  if (*type == "bookmarks.edit") {
    std::optional<BookmarkId> id = IdField(message, "id", model);
    std::optional<std::string> name =
        StringField(message, "name", BookmarkModel::kMaxNameBytes);
    std::optional<std::string> profile = StringField(message, "profile", 64);
    if (!id || BookmarkModel::IsRoot(*id) || !name || !profile ||
        !ProfileExists(delegate, *profile)) {
      return fail("Invalid request.");
    }
    const BookmarkItem* item = model.Find(*id);
    std::optional<std::string> url;
    if (!item->folder) {
      url = StringField(message, "url", BookmarkModel::kMaxUrlBytes);
      if (!url) {
        return fail("Invalid request.");
      }
      url = NormalizedUrl(*url);
      if (!url) {
        return fail("Enter a web address, e.g. https://example.com");
      }
    }
    std::optional<BookmarkId> parent;
    if (message->HasKey("parent")) {
      parent = IdField(message, "parent", model);
      if (!parent) {
        return fail("Invalid request.");
      }
    }
    std::optional<Error> error;
    delegate.Change([&](BookmarkModel& changed) {
      const BookmarkItem* current = changed.Find(*id);
      if (current->name != *name) {
        error = changed.Rename(*id, *name);
      }
      if (!error && url && current->url != *url) {
        error = changed.SetUrl(*id, *url);
      }
      if (!error && parent && *parent != current->parent) {
        error = changed.Move(*id, *parent, BookmarkModel::kEnd);
      }
      if (!error && changed.InheritedProfile(*id).empty() &&
          changed.Find(*id)->profile != *profile) {
        error = changed.SetProfile(*id, *profile);
      }
    });
    if (error) {
      return fail(error->message);
    }
    callback->Success("");
    return true;
  }
  if (*type == "bookmarks.move") {
    std::optional<std::vector<BookmarkId>> ids = IdsField(message, model);
    std::optional<BookmarkId> parent = IdField(message, "parent", model);
    std::optional<std::int64_t> index = NumberField(message, "index");
    if (!ids || !parent) {
      return fail("Invalid request.");
    }
    std::size_t at =
        index ? static_cast<std::size_t>(*index) : BookmarkModel::kEnd;
    std::optional<Error> error;
    delegate.Change([&](BookmarkModel& changed) {
      error = changed.MoveItems(*ids, *parent, at);
    });
    if (error) {
      return fail(error->message);
    }
    callback->Success("");
    return true;
  }
  if (*type == "bookmarks.remove") {
    std::optional<std::vector<BookmarkId>> ids = IdsField(message, model);
    if (!ids) {
      return fail("Invalid request.");
    }
    std::vector<BookmarkId> removable;
    for (BookmarkId id : model.WithoutNested(*ids)) {
      if (!BookmarkModel::IsRoot(id)) {
        removable.push_back(id);
      }
    }
    delegate.Remove(removable);
    callback->Success("");
    return true;
  }
  if (*type == "bookmarks.undo") {
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetBool("undone", delegate.Undo());
    callback->Success(ToJson(answer));
    return true;
  }
  if (*type == "bookmarks.open") {
    std::optional<BookmarkId> id = IdField(message, "id", model);
    if (!id || model.Find(*id)->folder) {
      return fail("Invalid request.");
    }
    bool new_tab =
        message->GetType("newTab") == VTYPE_BOOL && message->GetBool("newTab");
    delegate.Open(*id, new_tab);
    callback->Success("");
    return true;
  }
  if (*type == "bookmarks.export") {
    delegate.SaveHtml(ExportBookmarksHtml(model),
                      [callback](bool saved, std::optional<Error> error) {
                        if (error) {
                          callback->Failure(kFailed, error->message);
                          return;
                        }
                        CefRefPtr<CefDictionaryValue> answer =
                            CefDictionaryValue::Create();
                        answer->SetBool("saved", saved);
                        callback->Success(ToJson(answer));
                      });
    return true;
  }
  if (*type == "bookmarks.import") {
    std::weak_ptr<BookmarksPageDelegate> weak = delegate_;
    delegate.OpenHtml([weak, callback](std::optional<std::string> html,
                                       std::optional<Error> error) {
      std::shared_ptr<BookmarksPageDelegate> owner = weak.lock();
      if (error || !owner) {
        callback->Failure(kFailed,
                          error ? error->message : "The page was closed.");
        return;
      }
      CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
      answer->SetBool("imported", false);
      if (html && html->size() > kMaxImportBytes) {
        callback->Failure(kFailed, "The file is too large.");
        return;
      }
      if (html) {
        HtmlImportResult result;
        owner->Change([&](BookmarkModel& changed) {
          result = ImportBookmarksHtml(changed, *html, Now());
        });
        answer->SetBool("imported", result.folder != 0);
        answer->SetInt("bookmarks", result.bookmarks);
        answer->SetInt("folders", result.folders);
      }
      callback->Success(ToJson(answer));
    });
    return true;
  }
  return fail("Invalid request.");
}

}  // namespace brouk
