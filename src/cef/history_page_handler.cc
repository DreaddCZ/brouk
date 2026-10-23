#include "cef/history_page_handler.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cef/json_util.h"
#include "core/history_store.h"
#include "core/internal_pages.h"
#include "core/profile_registry.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxRequestBytes = std::size_t{256} * 1024;
constexpr std::size_t kMaxFieldBytes = 8192;
constexpr std::size_t kMaxDeleteItems = 5000;
constexpr int kMaxVisits = 500;
constexpr int kBadRequest = 400;
constexpr int kFailed = 500;

using Callback = CefMessageRouterBrowserSide::Callback;

std::optional<std::string> StringField(
    const CefRefPtr<CefDictionaryValue>& message,
    const char* key) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > kMaxFieldBytes) {
    return std::nullopt;
  }
  return value;
}

// Seconds since 1970, sent as a JSON number (exact up to 2^53).
std::optional<std::int64_t> TimeField(
    const CefRefPtr<CefDictionaryValue>& message,
    const char* key) {
  if (message->GetType(key) == VTYPE_INT) {
    return message->GetInt(key);
  }
  if (message->GetType(key) == VTYPE_DOUBLE) {
    double value = message->GetDouble(key);
    if (value >= 0 && value < 9.0e15) {
      return static_cast<std::int64_t>(value);
    }
  }
  return std::nullopt;
}

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::string_view TransitionName(VisitTransition transition) {
  switch (transition) {
    case VisitTransition::kLink:
      return "link";
    case VisitTransition::kTyped:
      return "typed";
    case VisitTransition::kBookmark:
      return "bookmark";
    case VisitTransition::kReload:
      return "reload";
    case VisitTransition::kOther:
      return "other";
  }
  return "other";
}

// The profiles a request covers: one (if it exists) or all (no "profile").
std::optional<std::vector<ProfileId>> ProfilesOf(
    const CefRefPtr<CefDictionaryValue>& message,
    HistoryPageDelegate& delegate) {
  std::vector<ProfileId> all;
  for (const ProfileInfo& info : delegate.Profiles()) {
    all.push_back(info.id);
  }
  if (message->GetType("profile") == VTYPE_NULL ||
      !message->HasKey("profile")) {
    return all;
  }
  std::optional<std::string> profile = StringField(message, "profile");
  if (!profile || std::ranges::find(all, *profile) == all.end()) {
    return std::nullopt;
  }
  return std::vector<ProfileId>{*profile};
}

// Runs `work` on each profile's history, then `finish` once all are done.
// Each profile writes only its own slot of the shared results.
template <typename Row>
void ForEachProfile(
    HistoryService& history,
    const std::vector<ProfileId>& profiles,
    const std::function<std::vector<Row>(HistoryStore&)>& work,
    const std::function<
        void(std::vector<std::pair<ProfileId, std::vector<Row>>>)>& finish) {
  struct Shared {
    std::vector<std::pair<ProfileId, std::vector<Row>>> results;
    std::size_t pending = 0;
  };
  auto shared = std::make_shared<Shared>();
  shared->pending = profiles.size();
  if (profiles.empty()) {
    finish({});
    return;
  }
  for (const ProfileId& profile : profiles) {
    shared->results.emplace_back(profile, std::vector<Row>{});
  }
  for (std::size_t i = 0; i < profiles.size(); ++i) {
    history.Run(
        profiles[i],
        [shared, i, work](HistoryStore& store) {
          shared->results[i].second = work(store);
        },
        [shared, finish] {
          if (--shared->pending == 0) {
            finish(std::move(shared->results));
          }
        });
  }
}

void AnswerSites(HistoryService& history,
                 const std::vector<ProfileId>& profiles,
                 const CefRefPtr<Callback>& callback) {
  ForEachProfile<HistorySite>(
      history, profiles, [](HistoryStore& store) { return store.Sites(); },
      [callback](const auto& results) {
        CefRefPtr<CefListValue> list = CefListValue::Create();
        for (const auto& [profile, sites] : results) {
          for (const HistorySite& site : sites) {
            CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
            row->SetString("profile", profile);
            row->SetString("host", site.host);
            row->SetDouble("visits", static_cast<double>(site.visit_count));
            row->SetDouble("pages", static_cast<double>(site.page_count));
            row->SetDouble("lastVisit", static_cast<double>(site.last_visit));
            list->SetDictionary(list->GetSize(), row);
          }
        }
        CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
        answer->SetList("sites", list);
        callback->Success(ToJson(answer));
      });
}

void AnswerPages(HistoryService& history,
                 const ProfileId& profile,
                 const std::string& host,
                 const CefRefPtr<Callback>& callback) {
  ForEachProfile<HistoryMatch>(
      history, {profile},
      [host](HistoryStore& store) { return store.PagesOfHost(host); },
      [callback](const auto& results) {
        CefRefPtr<CefListValue> list = CefListValue::Create();
        for (const HistoryMatch& page : results.front().second) {
          CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
          row->SetString("url", page.url);
          row->SetString("title", page.title);
          row->SetDouble("visits", static_cast<double>(page.visit_count));
          row->SetDouble("lastVisit", static_cast<double>(page.last_visit));
          list->SetDictionary(list->GetSize(), row);
        }
        CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
        answer->SetList("pages", list);
        callback->Success(ToJson(answer));
      });
}

void AnswerVisits(HistoryService& history,
                  const std::vector<ProfileId>& profiles,
                  std::int64_t before,
                  int limit,
                  const CefRefPtr<Callback>& callback) {
  ForEachProfile<HistoryVisit>(
      history, profiles,
      [before, limit](HistoryStore& store) {
        return store.Visits(before, static_cast<std::size_t>(limit));
      },
      [callback, limit](const auto& results) {
        // Newest first across profiles, at most `limit`.
        std::vector<std::pair<const ProfileId*, const HistoryVisit*>> merged;
        for (const auto& [profile, visits] : results) {
          for (const HistoryVisit& visit : visits) {
            merged.emplace_back(&profile, &visit);
          }
        }
        std::ranges::stable_sort(merged, [](const auto& a, const auto& b) {
          return a.second->time > b.second->time;
        });
        if (merged.size() > static_cast<std::size_t>(limit)) {
          merged.resize(static_cast<std::size_t>(limit));
        }
        CefRefPtr<CefListValue> list = CefListValue::Create();
        for (const auto& [profile, visit] : merged) {
          CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
          row->SetString("profile", *profile);
          row->SetDouble("id", static_cast<double>(visit->id));
          row->SetString("url", visit->url);
          row->SetString("title", visit->title);
          row->SetString("host", visit->host);
          row->SetDouble("time", static_cast<double>(visit->time));
          row->SetString("transition",
                         std::string(TransitionName(visit->transition)));
          list->SetDictionary(list->GetSize(), row);
        }
        CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
        answer->SetList("visits", list);
        callback->Success(ToJson(answer));
      });
}

// One thing to delete, checked.
struct DeleteItem {
  ProfileId profile;
  std::string kind;  // "page", "site" or "visit".
  std::string text;  // URL or host.
  std::int64_t id = 0;
};

std::optional<std::vector<DeleteItem>> DeleteItems(
    const CefRefPtr<CefDictionaryValue>& message,
    const std::vector<ProfileInfo>& profiles) {
  if (message->GetType("items") != VTYPE_LIST) {
    return std::nullopt;
  }
  CefRefPtr<CefListValue> list = message->GetList("items");
  if (list->GetSize() > kMaxDeleteItems) {
    return std::nullopt;
  }
  std::vector<DeleteItem> items;
  for (std::size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_DICTIONARY) {
      return std::nullopt;
    }
    CefRefPtr<CefDictionaryValue> entry = list->GetDictionary(i);
    std::optional<std::string> profile = StringField(entry, "profile");
    std::optional<std::string> kind = StringField(entry, "kind");
    if (!profile || !kind ||
        std::ranges::none_of(profiles, [&](const ProfileInfo& info) {
          return info.id == *profile;
        })) {
      return std::nullopt;
    }
    DeleteItem item{.profile = *profile, .kind = *kind, .text = {}, .id = 0};
    if (*kind == "visit") {
      std::optional<std::int64_t> id = TimeField(entry, "id");
      if (!id) {
        return std::nullopt;
      }
      item.id = *id;
    } else if (*kind == "page" || *kind == "site") {
      std::optional<std::string> text = StringField(entry, "value");
      if (!text) {
        return std::nullopt;
      }
      item.text = *text;
    } else {
      return std::nullopt;
    }
    items.push_back(std::move(item));
  }
  return items;
}

void DeleteAndAnswer(HistoryService& history,
                     std::vector<DeleteItem> items,
                     const CefRefPtr<Callback>& callback) {
  std::vector<ProfileId> profiles;
  for (const DeleteItem& item : items) {
    if (std::ranges::find(profiles, item.profile) == profiles.end()) {
      profiles.push_back(item.profile);
    }
  }
  auto shared = std::make_shared<std::vector<DeleteItem>>(std::move(items));
  // Each profile's deletions run on the history thread; then one answer.
  struct Pending {
    std::size_t left = 0;
  };
  auto pending = std::make_shared<Pending>();
  pending->left = profiles.size();
  if (profiles.empty()) {
    callback->Success("");
    return;
  }
  for (const ProfileId& profile : profiles) {
    history.Run(
        profile,
        [shared, profile](HistoryStore& store) {
          std::vector<std::int64_t> visits;
          for (const DeleteItem& item : *shared) {
            if (item.profile != profile) {
              continue;
            }
            if (item.kind == "page") {
              store.DeletePage(item.text);
            } else if (item.kind == "site") {
              store.DeleteHost(item.text);
            } else {
              visits.push_back(item.id);
            }
          }
          store.DeleteVisits(visits);
        },
        [pending, callback] {
          if (--pending->left == 0) {
            callback->Success("");
          }
        });
  }
}

}  // namespace

bool HistoryPageHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                                 CefRefPtr<CefFrame> frame,
                                 int64_t /*query_id*/,
                                 const CefString& request,
                                 bool persistent,
                                 CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  // Only the history page itself (the browser's own view of the frame).
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "history") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed = text.size() <= kMaxRequestBytes && !persistent
                                   ? ParseJson(text)
                                   : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::optional<std::string> type =
      message ? StringField(message, "type") : std::nullopt;
  if (!type) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  HistoryPageDelegate& delegate = *delegate_;

  if (*type == "history.state") {
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
    answer->SetString("view", delegate.View());
    callback->Success(ToJson(answer));
    return true;
  }
  if (*type == "history.setView") {
    std::optional<std::string> view = StringField(message, "view");
    if (view != "site" && view != "day") {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    delegate.SetView(*view);
    callback->Success("");
    return true;
  }
  if (*type == "history.open") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    std::optional<std::string> url = StringField(message, "url");
    bool new_tab =
        message->GetType("newTab") == VTYPE_BOOL && message->GetBool("newTab");
    // Only pages a history can hold: never brouk://, javascript: and such.
    if (!profiles || profiles->size() != 1 || !message->HasKey("profile") ||
        !url || !ShouldRecordInHistory(*url)) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    delegate.OpenPage(profiles->front(), *url, new_tab);
    callback->Success("");
    return true;
  }

  HistoryService* history = delegate.History();
  if (!history) {
    callback->Failure(kFailed, "The history is not available yet.");
    return true;
  }
  if (*type == "history.sites") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    if (!profiles) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    AnswerSites(*history, *profiles, callback);
  } else if (*type == "history.pages") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    std::optional<std::string> host = StringField(message, "host");
    if (!profiles || profiles->size() != 1 || !message->HasKey("profile") ||
        !host) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    AnswerPages(*history, profiles->front(), *host, callback);
  } else if (*type == "history.visits") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    std::optional<std::int64_t> before = TimeField(message, "before");
    std::optional<std::int64_t> limit = TimeField(message, "limit");
    if (!profiles || !before || !limit || *limit < 1 || *limit > kMaxVisits) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    AnswerVisits(*history, *profiles, *before, static_cast<int>(*limit),
                 callback);
  } else if (*type == "history.delete") {
    std::optional<std::vector<DeleteItem>> items =
        DeleteItems(message, delegate.Profiles());
    if (!items) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    DeleteAndAnswer(*history, std::move(*items), callback);
  } else {
    callback->Failure(kBadRequest, "Invalid request.");
  }
  return true;
}

}  // namespace brouk
