#include "cef/downloads_page_handler.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "cef/json_util.h"
#include "core/internal_pages.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxRequestBytes = std::size_t{16} * 1024;
constexpr std::size_t kListLimit = 5000;
constexpr int kBadRequest = 400;
constexpr double kMaxId = 9.0e15;

using Callback = CefMessageRouterBrowserSide::Callback;

std::string ToJson(const CefRefPtr<CefValue>& value) {
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

std::optional<std::string> Text(const CefRefPtr<CefDictionaryValue>& message,
                                const char* key) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > 256) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::int64_t> Id(const CefRefPtr<CefDictionaryValue>& message) {
  if (message->GetType("id") == VTYPE_INT) {
    return message->GetInt("id");
  }
  if (message->GetType("id") != VTYPE_DOUBLE) {
    return std::nullopt;
  }
  double number = message->GetDouble("id");
  if (number < 1 || number > kMaxId ||
      number != static_cast<double>(static_cast<std::int64_t>(number))) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(number);
}

std::vector<ProfileId> ProfileIds(DownloadsPageDelegate& delegate,
                                  const std::string& only) {
  std::vector<ProfileId> ids;
  for (const ProfileInfo& info : delegate.Profiles()) {
    if (only.empty() || info.id == only) {
      ids.push_back(info.id);
    }
  }
  return ids;
}

}  // namespace

DownloadsPageHandler::DownloadsPageHandler(
    std::unique_ptr<DownloadsPageDelegate> delegate)
    : delegate_(std::move(delegate)),
      watchers_(std::make_shared<std::map<int64_t, CefRefPtr<Callback>>>()) {}

void DownloadsPageHandler::OnQueryCanceled(CefRefPtr<CefBrowser> /*browser*/,
                                           CefRefPtr<CefFrame> /*frame*/,
                                           int64_t query_id) {
  CEF_REQUIRE_UI_THREAD();
  watchers_->erase(query_id);
}

bool DownloadsPageHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                                   CefRefPtr<CefFrame> frame,
                                   int64_t query_id,
                                   const CefString& request,
                                   bool persistent,
                                   CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "downloads") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed =
      text.size() <= kMaxRequestBytes ? ParseJson(text) : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::optional<std::string> type =
      message ? Text(message, "type") : std::nullopt;
  DownloadService* service = delegate_->Service();
  if (!type || persistent != (*type == "downloads.watch") || !service) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  std::shared_ptr<DownloadsPageDelegate> delegate = delegate_;

  if (*type == "downloads.watch") {
    watchers_->emplace(query_id, callback);
    if (!watching_) {
      watching_ = true;
      std::weak_ptr<std::map<int64_t, CefRefPtr<Callback>>> weak = watchers_;
      delegate->Watch([weak] {
        if (auto watchers = weak.lock()) {
          for (const auto& [id, watcher] : *watchers) {
            watcher->Success("changed");
          }
        }
      });
    }
    return true;
  }
  if (*type == "downloads.state") {
    CefRefPtr<CefListValue> profiles = CefListValue::Create();
    for (const ProfileInfo& info : delegate->Profiles()) {
      CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
      row->SetString("id", info.id);
      row->SetString("name", info.name);
      row->SetString("color", info.color);
      profiles->SetDictionary(profiles->GetSize(), row);
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetList("profiles", profiles);
    answer->SetString("currentProfile", delegate->CurrentProfile());
    CefRefPtr<CefValue> value = CefValue::Create();
    value->SetDictionary(answer);
    callback->Success(ToJson(value));
    return true;
  }
  std::optional<std::string> profile = Text(message, "profile");
  if (!profile) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }
  std::vector<ProfileId> profiles = ProfileIds(*delegate, *profile);
  if (!profile->empty() && profiles.empty()) {
    callback->Failure(kBadRequest, "There is no such profile.");
    return true;
  }
  if (*type == "downloads.list") {
    service->List(
        profiles, kListLimit,
        [delegate, callback](const std::vector<DownloadEntry>& entries) {
          CefRefPtr<CefListValue> list = CefListValue::Create();
          for (const DownloadEntry& entry : entries) {
            const DownloadRecord& record = entry.record;
            CefRefPtr<CefDictionaryValue> row = CefDictionaryValue::Create();
            row->SetString("profile", entry.profile);
            row->SetDouble("id", static_cast<double>(record.id));
            row->SetString("url", record.url);
            row->SetString("name", record.FileName());
            row->SetString("site", record.referrer_site);
            row->SetString("state",
                           std::string(DownloadStateName(record.state)));
            row->SetString("status", DownloadStatusText(record));
            row->SetDouble("received", static_cast<double>(record.received));
            row->SetDouble("total", static_cast<double>(record.total));
            row->SetDouble("start", static_cast<double>(record.start_time));
            row->SetBool("exists", delegate->FileExists(record.path));
            list->SetDictionary(list->GetSize(), row);
          }
          CefRefPtr<CefValue> value = CefValue::Create();
          value->SetList(list);
          callback->Success(ToJson(value));
        });
    return true;
  }
  if (*type == "downloads.clear") {
    service->ClearAll(profiles, [callback] { callback->Success(""); });
    return true;
  }
  if (*type == "downloads.action") {
    std::optional<std::int64_t> id = Id(message);
    std::optional<std::string> action = Text(message, "action");
    if (!id || !action || profile->empty()) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    const std::string& what = *action;
    ProfileId owner = *profile;
    std::int64_t target = *id;
    // The record as the browser knows it: the page sends only ids.
    service->List({owner}, kListLimit,
                  [delegate, service, callback, what, owner,
                   target](const std::vector<DownloadEntry>& entries) {
                    auto it = std::ranges::find_if(entries, [&](const auto& e) {
                      return e.record.id == target;
                    });
                    if (it == entries.end()) {
                      callback->Failure(kBadRequest, "The download is gone.");
                      return;
                    }
                    const DownloadRecord& record = it->record;
                    if (what == "open") {
                      delegate->OpenFile(record.path);
                    } else if (what == "reveal") {
                      delegate->RevealFile(record.path);
                    } else if (what == "copy") {
                      delegate->CopyText(record.url);
                    } else if (what == "pause") {
                      service->Pause(owner, target);
                    } else if (what == "resume") {
                      service->Resume(owner, target);
                    } else if (what == "cancel") {
                      service->Cancel(owner, target);
                    } else if (what == "retry") {
                      delegate->Retry(*it);
                    } else if (what == "remove") {
                      service->Remove(owner, target);
                    } else if (what == "delete") {
                      if (record.state == DownloadRecord::State::kComplete) {
                        delegate->TrashFile(record.path);
                      }
                      service->Remove(owner, target);
                    } else {
                      callback->Failure(kBadRequest, "Unknown action.");
                      return;
                    }
                    callback->Success("");
                  });
    return true;
  }
  callback->Failure(kBadRequest, "Unknown request.");
  return true;
}

}  // namespace brouk
