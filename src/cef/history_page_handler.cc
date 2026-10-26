#include "cef/history_page_handler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cef/json_util.h"
#include "core/history_query.h"
#include "core/history_sql.h"
#include "core/history_store.h"
#include "core/internal_pages.h"
#include "core/profile_registry.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Large enough for "Copy as CSV" (kMaxCopyBytes); every field has its own
// limit.
constexpr std::size_t kMaxRequestBytes = std::size_t{9} * 1024 * 1024;
constexpr std::size_t kMaxFieldBytes = 8192;
constexpr std::size_t kMaxDeleteItems = 5000;
constexpr int kMaxVisits = 500;
constexpr std::size_t kMaxSqlBytes = std::size_t{16} * 1024;
constexpr std::size_t kMaxCopyBytes = std::size_t{8} * 1024 * 1024;
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

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// The page's search text, parsed; empty if there is none.
std::shared_ptr<const HistoryQuery> QueryOf(
    const CefRefPtr<CefDictionaryValue>& message,
    std::string* text) {
  std::optional<std::string> query = StringField(message, "query");
  *text = query.value_or("");
  return std::make_shared<const HistoryQuery>(
      HistoryQuery::Parse(*text, Now()));
}

// UTF-16 length of the first `bytes` bytes of UTF-8 `text` (the page's
// string indices).
std::size_t Utf16Index(std::string_view text, std::size_t bytes) {
  std::size_t units = 0;
  for (std::size_t i = 0; i < bytes && i < text.size(); ++i) {
    auto c = static_cast<unsigned char>(text[i]);
    if ((c & 0xC0) == 0x80) {
      continue;  // Continuation byte.
    }
    units += c >= 0xF0 ? 2 : 1;  // Four-byte sequences are surrogate pairs.
  }
  return units;
}

CefRefPtr<CefListValue> ProblemsJson(const HistoryQuery& query,
                                     std::string_view text) {
  CefRefPtr<CefListValue> list = CefListValue::Create();
  for (const QueryProblem& problem : query.problems()) {
    CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
    row->SetInt("start", static_cast<int>(Utf16Index(text, problem.start)));
    row->SetInt("end", static_cast<int>(Utf16Index(text, problem.end)));
    row->SetString("message", problem.message);
    list->SetDictionary(list->GetSize(), row);
  }
  return list;
}

std::map<ProfileId, std::string> ProfileNames(HistoryPageDelegate& delegate) {
  std::map<ProfileId, std::string> names;
  for (const ProfileInfo& info : delegate.Profiles()) {
    names[info.id] = info.name;
  }
  return names;
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
    const std::function<std::vector<Row>(HistoryStore&, const ProfileId&)>&
        work,
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
          shared->results[i].second = work(store, shared->results[i].first);
        },
        [shared, finish] {
          if (--shared->pending == 0) {
            finish(std::move(shared->results));
          }
        });
  }
}

// Site icons (013) of the rows of an answer: each profile's images by icon
// id, filled on the history thread (one profile at a time), read on the UI
// thread once all are done.
class Icons {
 public:
  explicit Icons(const std::vector<ProfileId>& profiles) {
    for (const ProfileId& profile : profiles) {
      images_[profile];
    }
  }
  // Remembers the image of `id` (0: none) and returns the key rows use, ""
  // without an image.
  std::string Add(HistoryStore& store,
                  const ProfileId& profile,
                  std::int64_t id) {
    if (id == 0) {
      return {};
    }
    auto& images = images_.at(profile);
    auto it = images.find(id);
    if (it == images.end()) {
      it = images.emplace(id, store.IconImage(id)).first;
    }
    return it->second.empty() ? std::string() : Key(profile, id);
  }
  // {key: "data:image/png;base64,…"}.
  CefRefPtr<CefDictionaryValue> ToJson() const {
    CefRefPtr<CefDictionaryValue> json = CefDictionaryValue::Create();
    for (const auto& [profile, images] : images_) {
      for (const auto& [id, png] : images) {
        if (!png.empty()) {
          json->SetString(
              Key(profile, id),
              "data:image/png;base64," +
                  CefBase64Encode(png.data(), png.size()).ToString());
        }
      }
    }
    return json;
  }

 private:
  static std::string Key(const ProfileId& profile, std::int64_t id) {
    return profile + ":" + std::to_string(id);
  }
  std::map<ProfileId, std::map<std::int64_t, std::vector<std::uint8_t>>>
      images_;
};

void SetIcon(const CefRefPtr<CefDictionaryValue>& row, const std::string& key) {
  if (!key.empty()) {
    row->SetString("icon", key);
  }
}

struct Search {
  std::shared_ptr<const HistoryQuery> query;
  std::string text;
  std::map<ProfileId, std::string> names;
};

void AnswerSites(HistoryService& history,
                 const std::vector<ProfileId>& profiles,
                 const std::shared_ptr<const Search>& search,
                 const CefRefPtr<Callback>& callback) {
  auto icons = std::make_shared<Icons>(profiles);
  ForEachProfile<std::pair<HistorySite, std::string>>(
      history, profiles,
      [search, icons](HistoryStore& store, const ProfileId& profile) {
        std::vector<HistorySite> sites =
            search->query->empty()
                ? store.Sites()
                : store.SearchSites(*search->query, search->names.at(profile));
        std::vector<std::pair<HistorySite, std::string>> rows;
        for (HistorySite& site : sites) {
          std::string icon =
              icons->Add(store, profile, store.HostIcon(site.host));
          rows.emplace_back(std::move(site), std::move(icon));
        }
        return rows;
      },
      [callback, search, icons](const auto& results) {
        CefRefPtr<CefListValue> list = CefListValue::Create();
        for (const auto& [profile, sites] : results) {
          for (const auto& [site, icon] : sites) {
            CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
            row->SetString("profile", profile);
            row->SetString("host", site.host);
            SetIcon(row, icon);
            row->SetDouble("visits", static_cast<double>(site.visit_count));
            row->SetDouble("pages", static_cast<double>(site.page_count));
            row->SetDouble("lastVisit", static_cast<double>(site.last_visit));
            list->SetDictionary(list->GetSize(), row);
          }
        }
        CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
        answer->SetList("sites", list);
        answer->SetDictionary("icons", icons->ToJson());
        answer->SetList("problems", ProblemsJson(*search->query, search->text));
        callback->Success(ToJson(answer));
      });
}

void AnswerPages(HistoryService& history,
                 const ProfileId& profile,
                 const std::string& host,
                 const std::shared_ptr<const Search>& search,
                 const CefRefPtr<Callback>& callback) {
  std::string name = search->names.at(profile);
  auto icons = std::make_shared<Icons>(std::vector<ProfileId>{profile});
  ForEachProfile<std::pair<HistoryMatch, std::string>>(
      history, {profile},
      [host, search, name, icons](HistoryStore& store,
                                  const ProfileId& profile) {
        std::vector<HistoryMatch> pages =
            search->query->empty()
                ? store.PagesOfHost(host)
                : store.SearchPagesOfHost(host, *search->query, name);
        std::vector<std::pair<HistoryMatch, std::string>> rows;
        for (HistoryMatch& page : pages) {
          std::string icon =
              icons->Add(store, profile, store.PageIcon(page.url));
          rows.emplace_back(std::move(page), std::move(icon));
        }
        return rows;
      },
      [callback, icons](const auto& results) {
        CefRefPtr<CefListValue> list = CefListValue::Create();
        for (const auto& [page, icon] : results.front().second) {
          CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
          row->SetString("url", page.url);
          SetIcon(row, icon);
          row->SetString("title", page.title);
          row->SetDouble("visits", static_cast<double>(page.visit_count));
          row->SetDouble("lastVisit", static_cast<double>(page.last_visit));
          list->SetDictionary(list->GetSize(), row);
        }
        CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
        answer->SetList("pages", list);
        answer->SetDictionary("icons", icons->ToJson());
        callback->Success(ToJson(answer));
      });
}

void AnswerVisits(HistoryService& history,
                  const std::vector<ProfileId>& profiles,
                  std::int64_t before,
                  int limit,
                  const std::shared_ptr<const Search>& search,
                  const CefRefPtr<Callback>& callback) {
  auto icons = std::make_shared<Icons>(profiles);
  ForEachProfile<std::pair<HistoryVisit, std::string>>(
      history, profiles,
      [before, limit, search, icons](HistoryStore& store,
                                     const ProfileId& profile) {
        auto count = static_cast<std::size_t>(limit);
        std::vector<HistoryVisit> visits =
            search->query->empty()
                ? store.Visits(before, count)
                : store.SearchVisits(*search->query, search->names.at(profile),
                                     before, count);
        std::vector<std::pair<HistoryVisit, std::string>> rows;
        std::map<std::string, std::int64_t> page_icons;
        for (HistoryVisit& visit : visits) {
          auto known = page_icons.find(visit.url);
          if (known == page_icons.end()) {
            known =
                page_icons.emplace(visit.url, store.PageIcon(visit.url)).first;
          }
          std::string icon = icons->Add(store, profile, known->second);
          rows.emplace_back(std::move(visit), std::move(icon));
        }
        return rows;
      },
      [callback, limit, search, icons](const auto& results) {
        // Newest first across profiles, at most `limit`.
        std::vector<std::tuple<const ProfileId*, const HistoryVisit*,
                               const std::string*>>
            merged;
        for (const auto& [profile, visits] : results) {
          for (const auto& [visit, icon] : visits) {
            merged.emplace_back(&profile, &visit, &icon);
          }
        }
        std::ranges::stable_sort(merged, [](const auto& a, const auto& b) {
          return std::get<1>(a)->time > std::get<1>(b)->time;
        });
        if (merged.size() > static_cast<std::size_t>(limit)) {
          merged.resize(static_cast<std::size_t>(limit));
        }
        CefRefPtr<CefListValue> list = CefListValue::Create();
        for (const auto& [profile, visit, icon] : merged) {
          CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
          row->SetString("profile", *profile);
          SetIcon(row, *icon);
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
        answer->SetDictionary("icons", icons->ToJson());
        answer->SetList("problems", ProblemsJson(*search->query, search->text));
        callback->Success(ToJson(answer));
      });
}

// Cells are null, a number (integers and reals that JSON holds exactly) or
// text.
std::string SqlJson(const SqlResult& result) {
  CefRefPtr<CefListValue> columns = CefListValue::Create();
  for (const std::string& column : result.columns) {
    columns->SetString(columns->GetSize(), column);
  }
  CefRefPtr<CefListValue> rows = CefListValue::Create();
  for (const std::vector<SqlCell>& row : result.rows) {
    CefRefPtr<CefListValue> cells = CefListValue::Create();
    for (const SqlCell& cell : row) {
      std::size_t index = cells->GetSize();
      if (cell.type == SqlCell::Type::kNull) {
        cells->SetNull(index);
        continue;
      }
      if (cell.type == SqlCell::Type::kInteger ||
          cell.type == SqlCell::Type::kReal) {
        char* end = nullptr;
        double number = std::strtod(cell.text.c_str(), &end);
        if (end && *end == '\0' && std::abs(number) < 9.0e15) {
          cells->SetDouble(index, number);
          continue;
        }
      }
      cells->SetString(index, cell.text);
    }
    rows->SetList(rows->GetSize(), cells);
  }
  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  answer->SetList("columns", columns);
  answer->SetList("rows", rows);
  answer->SetBool("truncated", result.truncated);
  return ToJson(answer);
}

// Clear browsing data (013): per profile, the visits and sites in the time
// range from the history, and cookies and cache size from the delegate.
void PreviewClear(HistoryService& history,
                  HistoryPageDelegate& delegate,
                  const std::vector<ProfileId>& profiles,
                  std::int64_t since,
                  const CefRefPtr<Callback>& callback) {
  struct Row {
    std::int64_t visits = 0;
    std::int64_t sites = 0;
    int cookies = 0;
    std::int64_t cache_bytes = 0;
  };
  struct Shared {
    std::vector<Row> rows;
    std::size_t pending = 0;
  };
  auto shared = std::make_shared<Shared>();
  shared->rows.resize(profiles.size());
  shared->pending = profiles.size() * 2;
  auto finish = [shared, profiles, callback] {
    if (--shared->pending != 0) {
      return;
    }
    CefRefPtr<CefListValue> list = CefListValue::Create();
    for (std::size_t i = 0; i < profiles.size(); ++i) {
      const Row& row = shared->rows[i];
      CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
      item->SetString("profile", profiles[i]);
      item->SetDouble("visits", static_cast<double>(row.visits));
      item->SetDouble("sites", static_cast<double>(row.sites));
      item->SetInt("cookies", row.cookies);
      item->SetDouble("cacheBytes", static_cast<double>(row.cache_bytes));
      list->SetDictionary(list->GetSize(), item);
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetList("profiles", list);
    callback->Success(ToJson(answer));
  };
  if (profiles.empty()) {
    shared->pending = 1;
    finish();
    return;
  }
  for (std::size_t i = 0; i < profiles.size(); ++i) {
    history.Run(
        profiles[i],
        [shared, i, since](HistoryStore& store) {
          shared->rows[i].visits = store.CountVisitsSince(since);
          shared->rows[i].sites =
              static_cast<std::int64_t>(store.HostsSince(since).size());
        },
        finish);
    delegate.PreviewProfileData(
        profiles[i], since,
        [shared, i, finish](int cookies, std::int64_t cache_bytes) {
          shared->rows[i].cookies = cookies;
          shared->rows[i].cache_bytes = cache_bytes;
          finish();
        });
  }
}

struct ClearRequest {
  std::vector<ProfileId> profiles;
  std::int64_t since = 0;
  bool history = false;
  bool cookies = false;
  bool cache = false;
  bool downloads = false;  // The downloads list (014); never the files.
};

// One profile after the other: the sites of the time range are read from
// the history first (their site data goes with the cookies), then cookies,
// site data and cache, then the history itself.
void RunClear(HistoryService& history,
              const std::shared_ptr<HistoryPageDelegate>& delegate,
              ClearRequest request,
              const CefRefPtr<Callback>& callback) {
  struct State {
    ClearRequest request;
    std::size_t next = 0;
    std::set<std::string> sites;
    bool cookies_failed = false;
  };
  auto state = std::make_shared<State>();
  state->request = std::move(request);
  auto step = std::make_shared<std::function<void()>>();
  HistoryService* service = &history;
  const std::shared_ptr<HistoryPageDelegate>& page = delegate;
  *step = [state, step, service, page, callback]() {
    if (state->next >= state->request.profiles.size()) {
      CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
      answer->SetBool("cookiesFailed", state->cookies_failed);
      callback->Success(ToJson(answer));
      *step = nullptr;  // Breaks the cycle of the step and its lambda.
      return;
    }
    ProfileId profile = state->request.profiles[state->next++];
    std::int64_t since = state->request.since;
    service->Run(
        profile,
        [state, since](HistoryStore& store) {
          state->sites.clear();
          for (const HistorySite& site : store.HostsSince(since)) {
            state->sites.insert(site.host);
          }
        },
        [state, step, service, page, profile, since] {
          page->ClearProfileData(
              profile, since, state->request.cookies, state->request.cache,
              state->sites,
              [state, step, service, profile, since](bool cookies_ok) {
                state->cookies_failed |= !cookies_ok;
                bool history = state->request.history;
                bool downloads = state->request.downloads;
                if (!history && !downloads) {
                  (*step)();
                  return;
                }
                service->Run(
                    profile,
                    [since, history, downloads](HistoryStore& store) {
                      if (history) {
                        store.DeleteVisitsSince(since);
                      }
                      if (downloads) {
                        store.DeleteDownloadsSince(since);
                      }
                    },
                    [step] { (*step)(); });
              });
        });
  };
  (*step)();
}

// What Clean up does in one profile: the sites whose visits go, and (in
// "keep" mode) the sites kept, which become the keep list.
struct CleanupPlan {
  ProfileId profile;
  std::vector<std::string> remove;
  std::optional<std::vector<std::string>> keep;
};

constexpr std::size_t kMaxCleanupHosts = 50000;

std::optional<std::vector<std::string>> HostList(
    const CefRefPtr<CefDictionaryValue>& entry,
    const char* key) {
  if (entry->GetType(key) != VTYPE_LIST) {
    return std::nullopt;
  }
  CefRefPtr<CefListValue> list = entry->GetList(key);
  if (list->GetSize() > kMaxCleanupHosts) {
    return std::nullopt;
  }
  std::vector<std::string> hosts;
  for (std::size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_STRING) {
      return std::nullopt;
    }
    std::string host = list->GetString(i).ToString();
    if (host.size() > 255) {
      return std::nullopt;
    }
    hosts.push_back(std::move(host));
  }
  return hosts;
}

std::optional<std::vector<CleanupPlan>> CleanupPlans(
    const CefRefPtr<CefDictionaryValue>& message,
    const std::vector<ProfileInfo>& profiles) {
  if (message->GetType("profiles") != VTYPE_LIST) {
    return std::nullopt;
  }
  CefRefPtr<CefListValue> list = message->GetList("profiles");
  if (list->GetSize() > profiles.size()) {
    return std::nullopt;
  }
  std::vector<CleanupPlan> plans;
  for (std::size_t i = 0; i < list->GetSize(); ++i) {
    if (list->GetType(i) != VTYPE_DICTIONARY) {
      return std::nullopt;
    }
    CefRefPtr<CefDictionaryValue> entry = list->GetDictionary(i);
    std::optional<std::string> profile = StringField(entry, "profile");
    std::optional<std::vector<std::string>> remove = HostList(entry, "remove");
    if (!profile || !remove ||
        std::ranges::none_of(
            profiles,
            [&](const ProfileInfo& info) { return info.id == *profile; }) ||
        std::ranges::any_of(plans, [&](const CleanupPlan& plan) {
          return plan.profile == *profile;
        })) {
      return std::nullopt;
    }
    CleanupPlan plan{
        .profile = *profile, .remove = std::move(*remove), .keep = {}};
    if (entry->HasKey("keep")) {
      plan.keep = HostList(entry, "keep");
      if (!plan.keep) {
        return std::nullopt;
      }
    }
    plans.push_back(std::move(plan));
  }
  return plans;
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
    answer->SetString("currentProfile", delegate.CurrentProfile());
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
  if (*type == "history.copy") {
    // Not StringField: results can be bigger than other fields.
    if (message->GetType("text") != VTYPE_STRING) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    std::string copied = message->GetString("text").ToString();
    if (copied.size() > kMaxCopyBytes) {
      callback->Failure(kBadRequest, "Too much to copy.");
      return true;
    }
    delegate.CopyText(copied);
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
    // Without a profile (an SQL result without one): the history tab's.
    bool own_profile = !message->HasKey("profile");
    if (!profiles || (!own_profile && profiles->size() != 1) || !url ||
        !ShouldRecordInHistory(*url)) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    delegate.OpenPage(own_profile ? ProfileId() : profiles->front(), *url,
                      new_tab);
    callback->Success("");
    return true;
  }

  HistoryService* history = delegate.History();
  if (!history) {
    callback->Failure(kFailed, "The history is not available yet.");
    return true;
  }
  auto search = std::make_shared<Search>();
  search->query = QueryOf(message, &search->text);
  search->names = ProfileNames(delegate);
  std::shared_ptr<const Search> shared_search = search;
  if (*type == "history.sites") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    if (!profiles) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    AnswerSites(*history, *profiles, shared_search, callback);
  } else if (*type == "history.pages") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    std::optional<std::string> host = StringField(message, "host");
    if (!profiles || profiles->size() != 1 || !message->HasKey("profile") ||
        !host) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    AnswerPages(*history, profiles->front(), *host, shared_search, callback);
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
                 shared_search, callback);
  } else if (*type == "history.sql") {
    if (message->GetType("query") != VTYPE_STRING ||
        message->GetString("query").ToString().size() > kMaxSqlBytes) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    std::vector<std::pair<ProfileId, std::string>> profiles;
    for (const ProfileInfo& info : delegate.Profiles()) {
      profiles.emplace_back(info.id, info.name);
    }
    history->RunSql(std::move(profiles), message->GetString("query").ToString(),
                    [callback](Result<SqlResult> result) {
                      if (!result) {
                        // SQLite's own message (013).
                        callback->Failure(kFailed, result.error().message);
                        return;
                      }
                      callback->Success(SqlJson(result.value()));
                    });
  } else if (*type == "history.cleanup.preview") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    std::optional<std::int64_t> since = TimeField(message, "since");
    if (!profiles || !since) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    CefRefPtr<CefDictionaryValue> keep_lists = CefDictionaryValue::Create();
    for (const ProfileId& profile : *profiles) {
      CefRefPtr<CefListValue> hosts = CefListValue::Create();
      for (const std::string& host : delegate.KeepList(profile)) {
        hosts->SetString(hosts->GetSize(), host);
      }
      keep_lists->SetList(profile, hosts);
    }
    std::int64_t from = *since;
    ForEachProfile<HistorySite>(
        *history, *profiles,
        [from](HistoryStore& store, const ProfileId& /*profile*/) {
          return store.HostsSince(from);
        },
        [callback, keep_lists](const auto& results) {
          CefRefPtr<CefListValue> rows = CefListValue::Create();
          for (const auto& [profile, sites] : results) {
            for (const HistorySite& site : sites) {
              CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
              row->SetString("profile", profile);
              row->SetString("host", site.host);
              row->SetDouble("visits", static_cast<double>(site.visit_count));
              row->SetDouble("lastVisit", static_cast<double>(site.last_visit));
              rows->SetDictionary(rows->GetSize(), row);
            }
          }
          CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
          answer->SetList("rows", rows);
          answer->SetDictionary("keepLists", keep_lists);
          callback->Success(ToJson(answer));
        });
  } else if (*type == "history.cleanup.run") {
    std::optional<std::vector<CleanupPlan>> plans =
        CleanupPlans(message, delegate.Profiles());
    std::optional<std::int64_t> since = TimeField(message, "since");
    if (!plans || !since) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    // The kept sites become each profile's keep list (013).
    std::vector<ProfileId> profiles;
    for (const CleanupPlan& plan : *plans) {
      if (plan.keep) {
        delegate.SetKeepList(plan.profile, *plan.keep);
      }
      profiles.push_back(plan.profile);
    }
    auto shared = std::make_shared<std::vector<CleanupPlan>>(std::move(*plans));
    std::int64_t from = *since;
    ForEachProfile<int>(
        *history, profiles,
        [shared, from](HistoryStore& store, const ProfileId& profile) {
          for (const CleanupPlan& plan : *shared) {
            if (plan.profile == profile) {
              return std::vector<int>{
                  store.DeleteHostVisitsSince(plan.remove, from)};
            }
          }
          return std::vector<int>{0};
        },
        [callback](const auto& results) {
          double removed = 0;
          for (const auto& [profile, counts] : results) {
            for (int count : counts) {
              removed += count;
            }
          }
          CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
          answer->SetDouble("visits", removed);
          callback->Success(ToJson(answer));
        });
  } else if (*type == "history.clear.preview") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    std::optional<std::int64_t> since = TimeField(message, "since");
    if (!profiles || !since) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    PreviewClear(*history, delegate, *profiles, *since, callback);
  } else if (*type == "history.clear.run") {
    std::optional<std::vector<ProfileId>> profiles =
        ProfilesOf(message, delegate);
    std::optional<std::int64_t> since = TimeField(message, "since");
    auto flag = [&](const char* key) {
      return message->GetType(key) == VTYPE_BOOL && message->GetBool(key);
    };
    if (!profiles || !since) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    RunClear(*history, delegate_,
             {.profiles = *profiles,
              .since = *since,
              .history = flag("history"),
              .cookies = flag("cookies"),
              .cache = flag("cache"),
              .downloads = flag("downloads")},
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
