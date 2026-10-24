#include "core/adblock_store.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/json.h"

namespace brouk {

namespace {

// Changes when cached engines can no longer be read (adblock-rust's format).
constexpr std::string_view kEngineCacheVersion = "adblock-0.13.3/1";
constexpr std::size_t kMaxCachedEngines = 6;
constexpr std::int64_t kRetryAfterErrorSeconds = 3600;

std::optional<std::string> ReadFile(const std::filesystem::path& path,
                                    std::size_t max_bytes) {
  std::error_code error;
  std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > max_bytes) {
    return std::nullopt;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }
  return std::string((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
}

// Written next to the target and renamed, so readers and crashes never see
// half a file.
std::optional<Error> WriteFile(const std::filesystem::path& path,
                               std::string_view data) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    file.close();
    if (!file) {
      std::filesystem::remove(temp, error);
      return Error{"cannot write " + temp.string()};
    }
  }
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Error{"cannot replace " + path.string() + ": " + error.message()};
  }
  return std::nullopt;
}

std::uint64_t Fnv(std::uint64_t hash, std::string_view data) {
  for (char c : data) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string Hex(std::uint64_t value) {
  constexpr std::string_view kHex = "0123456789abcdef";
  std::string text;
  for (int shift = 60; shift >= 0; shift -= 4) {
    text += kHex[(value >> shift) & 0xF];
  }
  return text;
}

bool SafeId(std::string_view id) {
  return !id.empty() && id.size() <= 64 && std::ranges::all_of(id, [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
           c == '_';
  });
}

}  // namespace

bool LooksLikeFilterList(std::string_view text) {
  // The first line that is not blank: web pages start with markup.
  std::size_t start = text.find_first_not_of(" \t\r\n\xEF\xBB\xBF");
  if (start == std::string_view::npos) {
    return false;
  }
  return text[start] != '<' && text[start] != '{';
}

AdblockStore::AdblockStore(std::filesystem::path dir) : dir_(std::move(dir)) {
  LoadStates();
}

std::filesystem::path AdblockStore::ListPath(std::string_view id) const {
  return dir_ / "lists" / (std::string(id) + ".txt");
}

void AdblockStore::LoadStates() {
  std::optional<std::string> text =
      ReadFile(dir_ / "lists.json", std::size_t{1} << 20);
  if (!text) {
    return;
  }
  Result<JsonValue> json = ParseJsonDocument(*text);
  if (!json || json.value().kind != JsonValue::Kind::kObject) {
    return;
  }
  for (const JsonMember& member : json.value().members) {
    if (!SafeId(member.key) || member.value.kind != JsonValue::Kind::kObject) {
      continue;
    }
    FilterListState state;
    state.updated = member.value.IntegerOr("updated", 0);
    state.attempted = member.value.IntegerOr("attempted", 0);
    std::int64_t expires = member.value.IntegerOr("expires_hours", 0);
    if (expires > 0) {
      state.expires_hours = static_cast<int>(expires);
    }
    state.error = member.value.StringOr("error", "");
    states_[member.key] = std::move(state);
  }
}

void AdblockStore::SaveStates() const {
  JsonValue json = JsonValue::Object();
  for (const auto& [id, state] : states_) {
    JsonValue item = JsonValue::Object();
    item.Add("updated", JsonValue::Integer(state.updated));
    item.Add("attempted", JsonValue::Integer(state.attempted));
    if (state.expires_hours) {
      item.Add("expires_hours", JsonValue::Integer(*state.expires_hours));
    }
    if (!state.error.empty()) {
      item.Add("error", JsonValue::String(state.error));
    }
    json.Add(id, std::move(item));
  }
  WriteFile(dir_ / "lists.json", WriteJsonDocument(json));
}

std::optional<std::string> AdblockStore::ReadList(std::string_view id) const {
  if (!SafeId(id)) {
    return std::nullopt;
  }
  return ReadFile(ListPath(id), kMaxFilterListBytes);
}

std::optional<Error> AdblockStore::SaveDownload(std::string_view id,
                                                std::string_view text,
                                                std::int64_t now) {
  if (!SafeId(id)) {
    return Error{"invalid list id"};
  }
  std::optional<Error> error;
  if (text.size() > kMaxFilterListBytes) {
    error = Error{"The list is too large."};
  } else if (!LooksLikeFilterList(text)) {
    error = Error{"The address did not return a filter list."};
  } else {
    error = WriteFile(ListPath(id), text);
  }
  if (error) {
    SaveError(id, error->message, now);
    return error;
  }
  FilterListState& state = states_[std::string(id)];
  state.updated = now;
  state.attempted = now;
  state.expires_hours = ParseExpiresHours(text);
  state.error.clear();
  SaveStates();
  return std::nullopt;
}

void AdblockStore::SaveError(std::string_view id,
                             std::string error,
                             std::int64_t now) {
  if (!SafeId(id)) {
    return;
  }
  FilterListState& state = states_[std::string(id)];
  state.attempted = now;
  state.error = std::move(error);
  SaveStates();
}

bool AdblockStore::IsDue(std::string_view id, std::int64_t now) const {
  auto it = states_.find(id);
  if (it == states_.end() || it->second.updated == 0) {
    return it == states_.end() ||
           now - it->second.attempted >= kRetryAfterErrorSeconds;
  }
  const FilterListState& state = it->second;
  if (!state.error.empty() && now - state.attempted < kRetryAfterErrorSeconds) {
    return false;
  }
  std::int64_t interval =
      std::int64_t{UpdateIntervalHours(state.expires_hours)} * 3600;
  return now - state.updated >= interval;
}

void AdblockStore::RemoveUnused(std::span<const std::string> used_ids) {
  std::error_code error;
  std::vector<std::string> unused;
  for (const auto& [id, state] : states_) {
    if (id != "resources" &&
        std::ranges::find(used_ids, id) == used_ids.end()) {
      unused.push_back(id);
    }
  }
  for (const std::string& id : unused) {
    std::filesystem::remove(ListPath(id), error);
    states_.erase(id);
  }
  if (!unused.empty()) {
    SaveStates();
  }
}

std::string AdblockStore::ReadMyFilters() const {
  return ReadFile(dir_ / "my-filters.txt", kMaxFilterListBytes).value_or("");
}

std::optional<Error> AdblockStore::SaveMyFilters(std::string_view text) {
  return WriteFile(dir_ / "my-filters.txt", text);
}

std::optional<std::string> AdblockStore::ReadResources() const {
  return ReadFile(dir_ / "resources.json", kMaxFilterListBytes);
}

std::optional<Error> AdblockStore::SaveResources(std::string_view json,
                                                 std::int64_t now) {
  Result<JsonValue> parsed = ParseJsonDocument(json);
  if (!parsed || parsed.value().kind != JsonValue::Kind::kArray) {
    SaveError("resources", "The resources could not be read.", now);
    return Error{"The resources could not be read."};
  }
  if (std::optional<Error> error = WriteFile(dir_ / "resources.json", json)) {
    SaveError("resources", error->message, now);
    return error;
  }
  FilterListState& state = states_["resources"];
  state.updated = now;
  state.attempted = now;
  state.error.clear();
  SaveStates();
  return std::nullopt;
}

BuiltEngine AdblockStore::LoadOrBuildEngine(
    std::span<const FilterListDef> lists,
    std::string_view resources_json) {
  std::vector<std::string> texts;
  std::vector<AdblockList> sources;
  BuiltEngine built;
  std::uint64_t key = Fnv(14695981039346656037ULL, kEngineCacheVersion);
  // Texts first: `sources` points into them.
  texts.reserve(lists.size() + 1);
  for (const FilterListDef& list : lists) {
    std::optional<std::string> text = ReadList(list.id);
    if (!text) {
      continue;  // Not downloaded yet.
    }
    texts.push_back(std::move(*text));
    built.list_ids.push_back(list.id);
    key = Fnv(key, list.id);
    key = Fnv(key, Hex(Fnv(14695981039346656037ULL, texts.back())));
  }
  std::string my_filters = ReadMyFilters();
  if (texts.empty() && my_filters.empty()) {
    return built;
  }
  texts.push_back(std::move(my_filters));
  built.list_ids.emplace_back(kMyFiltersId);
  key = Fnv(key, kMyFiltersId);
  key = Fnv(key, texts.back());
  for (std::size_t i = 0; i < texts.size(); ++i) {
    bool mine = i + 1 == texts.size();
    const FilterListDef* def = nullptr;
    for (const FilterListDef& list : lists) {
      if (list.id == built.list_ids[i]) {
        def = &list;
      }
    }
    sources.push_back({.text = texts[i],
                       .hosts_format = def != nullptr && def->hosts_format,
                       .trusted = mine || (def != nullptr && def->trusted)});
  }

  std::filesystem::path cache = dir_ / "engines" / (Hex(key) + ".dat");
  if (std::optional<std::string> bytes =
          ReadFile(cache, std::size_t{1} << 30)) {
    built.engine = AdblockEngine::Deserialize(std::span(
        reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()));
  }
  if (!built.engine) {
    built.engine = AdblockEngine::Build(sources);
    if (built.engine) {
      std::vector<std::uint8_t> bytes = built.engine->Serialize();
      WriteFile(cache,
                std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                 bytes.size()));
      // Keep the newest few (one per set of categories in use).
      std::error_code error;
      std::vector<std::filesystem::directory_entry> cached;
      for (const auto& entry :
           std::filesystem::directory_iterator(dir_ / "engines", error)) {
        cached.push_back(entry);
      }
      std::ranges::sort(cached, [](const auto& a, const auto& b) {
        std::error_code e;
        return a.last_write_time(e) > b.last_write_time(e);
      });
      for (std::size_t i = kMaxCachedEngines; i < cached.size(); ++i) {
        std::filesystem::remove(cached[i].path(), error);
      }
    }
  }
  if (built.engine && !resources_json.empty()) {
    built.engine->UseResources(resources_json);
  }
  return built;
}

}  // namespace brouk
