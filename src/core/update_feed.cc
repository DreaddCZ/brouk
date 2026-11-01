#include "core/update_feed.h"

#include <charconv>
#include <utility>

#include "core/crypto.h"
#include "core/json.h"

namespace brouk {

namespace {

// Larger than any installer Brouk will have; a feed cannot make Brouk
// download more.
constexpr std::int64_t kMaxInstallerBytes = std::int64_t{1} << 30;

std::optional<UpdateRelease> ReadRelease(const JsonValue& entry) {
  if (entry.kind != JsonValue::Kind::kObject) {
    return std::nullopt;
  }
  UpdateRelease release;
  release.version = entry.StringOr("version", "");
  release.channel = entry.StringOr("channel", "stable");
  release.url = entry.StringOr("url", "");
  release.size = entry.IntegerOr("size", 0);
  release.notes_url = entry.StringOr("notes_url", "");
  std::optional<std::string> signature =
      Base64Decode(entry.StringOr("ed_signature", ""));
  if (!ParseVersion(release.version) ||
      (release.channel != "stable" && release.channel != "beta") ||
      !release.url.starts_with("https://") || release.size <= 0 ||
      release.size > kMaxInstallerBytes || !signature ||
      signature->size() != 64 ||
      (!release.notes_url.empty() &&
       !release.notes_url.starts_with("https://"))) {
    return std::nullopt;
  }
  release.signature = std::move(*signature);
  return release;
}

}  // namespace

Result<std::vector<UpdateRelease>> ParseUpdateFeed(std::string_view json) {
  Result<JsonValue> document = ParseJsonDocument(json);
  if (!document) {
    return Error{"The update feed is not valid JSON: " +
                 document.error().message};
  }
  const JsonValue* list = document.value().kind == JsonValue::Kind::kObject
                              ? document.value().Find("releases")
                              : nullptr;
  if (!list || list->kind != JsonValue::Kind::kArray) {
    return Error{"The update feed has no list of releases."};
  }
  std::vector<UpdateRelease> releases;
  for (const JsonValue& entry : list->items) {
    if (std::optional<UpdateRelease> release = ReadRelease(entry)) {
      releases.push_back(std::move(*release));
    }
  }
  return releases;
}

std::optional<std::array<std::uint32_t, 3>> ParseVersion(
    std::string_view text) {
  std::array<std::uint32_t, 3> parts{};
  const char* position = text.data();
  const char* end = text.data() + text.size();
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      if (position == end || *position != '.') {
        return std::nullopt;
      }
      ++position;
    }
    if (position == end || *position < '0' || *position > '9') {
      return std::nullopt;
    }
    auto [next, error] = std::from_chars(position, end, parts.at(i));
    if (error != std::errc()) {
      return std::nullopt;
    }
    position = next;
  }
  if (position != end) {
    return std::nullopt;
  }
  return parts;
}

std::optional<UpdateRelease> ChooseUpdate(
    const std::vector<UpdateRelease>& releases,
    std::string_view current_version,
    std::string_view channel) {
  std::optional<std::array<std::uint32_t, 3>> best =
      ParseVersion(current_version);
  if (!best) {
    return std::nullopt;
  }
  const UpdateRelease* chosen = nullptr;
  for (const UpdateRelease& release : releases) {
    if (release.channel == "beta" && channel != "beta") {
      continue;
    }
    std::optional<std::array<std::uint32_t, 3>> version =
        ParseVersion(release.version);
    if (version && *version > *best) {
      best = version;
      chosen = &release;
    }
  }
  if (!chosen) {
    return std::nullopt;
  }
  return *chosen;
}

}  // namespace brouk
