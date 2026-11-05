#include "core/security_check.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <ranges>
#include <system_error>
#include <utility>

#include "core/web_origin.h"
#include "core/yaml_io.h"
#include "core/yaml_value.h"

namespace brouk {

namespace {

std::string_view WithoutFragment(std::string_view url) {
  return url.substr(0, url.find('#'));
}

// The site of a URL's host, or empty for URLs that are not http(s).
std::string SiteOfUrl(std::string_view url) {
  std::optional<std::string> origin = WebOriginOf(url);
  return origin ? SiteOfHost(HostOfOrigin(*origin)) : std::string();
}

// Keeps the newest `limit` entries.
void KeepNewest(std::vector<const RecordedRequest*>& list, std::size_t limit) {
  if (list.size() > limit) {
    list.erase(list.begin(),
               list.begin() + static_cast<std::ptrdiff_t>(list.size() - limit));
  }
}

}  // namespace

SecurityCheckRequests CollectSecurityCheckRequests(const RequestLog& log,
                                                   std::string_view page_url) {
  SecurityCheckRequests result;
  const std::deque<RecordedRequest>& entries = log.entries();
  const std::string_view page = WithoutFragment(page_url);
  // The page's last load; redirect hops are entries of their own, and the
  // last hop has the page's URL.
  auto document = std::ranges::find_if(
      std::views::reverse(entries), [page](const RecordedRequest& entry) {
        return entry.type == "Document" && WithoutFragment(entry.url) == page;
      });
  if (document == entries.rend()) {
    return result;
  }
  result.document = &*document;
  const std::string site = SiteOfUrl(page_url);
  // The document itself first (base() is the entry after it).
  for (auto it = std::prev(document.base()); it != entries.end(); ++it) {
    const RecordedRequest& entry = *it;
    result.page_requests.push_back(&entry);
    if ((entry.type == "Fetch" || entry.type == "XHR") && entry.status > 0 &&
        !site.empty() && SiteOfUrl(entry.url) == site) {
      result.site_requests.push_back(&entry);
    }
  }
  KeepNewest(result.page_requests, SecurityCheckRequests::kMaxPageRequests);
  KeepNewest(result.site_requests, SecurityCheckRequests::kMaxSiteRequests);
  return result;
}

std::string SecurityCheckSite(std::string_view page_url) {
  return SiteOfUrl(page_url);
}

bool IsFindingId(std::string_view id) {
  return !id.empty() && id.size() <= 200 &&
         std::ranges::all_of(id, [](char c) { return c > ' ' && c < 0x7f; });
}

HiddenFindings LoadHiddenFindings(const std::filesystem::path& path) {
  HiddenFindings hidden;
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return hidden;
  }
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  Result<YamlNode> root = ParseYaml(text);
  if (!root || root.value().kind != YamlNode::Kind::kMap) {
    return hidden;
  }
  const YamlNode* sites = root.value().Find("sites");
  if (!sites || sites->kind != YamlNode::Kind::kMap) {
    return hidden;
  }
  for (const YamlEntry& site : sites->entries) {
    if (hidden.size() >= kMaxHiddenSites || site.key.empty() ||
        site.value.kind != YamlNode::Kind::kSequence) {
      continue;
    }
    std::vector<std::string> ids;
    for (const YamlNode& item : site.value.items) {
      if (item.kind == YamlNode::Kind::kScalar && IsFindingId(item.scalar) &&
          ids.size() < kMaxHiddenPerSite &&
          std::ranges::find(ids, item.scalar) == ids.end()) {
        ids.push_back(item.scalar);
      }
    }
    if (!ids.empty()) {
      hidden[site.key] = std::move(ids);
    }
  }
  return hidden;
}

std::optional<Error> SaveHiddenFindings(const std::filesystem::path& path,
                                        const HiddenFindings& hidden) {
  YamlNode root = YamlNode::Map();
  YamlNode& sites = root.GetOrAdd("sites");
  sites.kind = YamlNode::Kind::kMap;
  for (const auto& [site, ids] : hidden) {
    if (ids.empty()) {
      continue;
    }
    YamlNode list;
    list.kind = YamlNode::Kind::kSequence;
    for (const std::string& id : ids) {
      list.items.push_back(YamlNode::Scalar(id, false));
    }
    sites.entries.push_back({site, std::move(list)});
  }
  std::string yaml = EmitYaml(root);
  if (yaml.empty()) {
    return Error{"The hidden findings could not be written."};
  }
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::filesystem::path temp = path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << "# Security check findings hidden per site (Brouk developer "
            "tools).\n"
         << yaml;
    if (!file) {
      return Error{"The hidden findings could not be written."};
    }
  }
  std::filesystem::permissions(
      temp,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      error);
  std::filesystem::rename(temp, path, error);
  if (error) {
    return Error{"The hidden findings could not be saved: " + error.message()};
  }
  return std::nullopt;
}

}  // namespace brouk
