#include "cef/profile_links.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/url_pattern.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxLinks = 200;
constexpr std::size_t kMaxPatternBytes = 1024;
constexpr int kBadRequest = 400;
constexpr int kFailed = 500;

}  // namespace

void SaveProfileLinks(
    const CefRefPtr<CefListValue>& patterns,
    const ProfileId& profile,
    const LinkRoutingConfig& current,
    const std::function<std::optional<Error>(LinkRoutingConfig)>& save,
    const CefRefPtr<CefMessageRouterBrowserSide::Callback>& callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!patterns || patterns->GetSize() > kMaxLinks) {
    callback->Failure(kBadRequest, "Invalid request.");
    return;
  }
  std::vector<std::string> valid;
  CefRefPtr<CefListValue> problems = CefListValue::Create();
  for (std::size_t i = 0; i < patterns->GetSize(); ++i) {
    std::string pattern = patterns->GetType(i) == VTYPE_STRING
                              ? patterns->GetString(i).ToString()
                              : "";
    if (pattern.size() > kMaxPatternBytes ||
        !UrlPattern::Parse(pattern, UrlPattern::Fragment::kAllowed)) {
      CefRefPtr<CefDictionaryValue> problem = CefDictionaryValue::Create();
      problem->SetInt("index", static_cast<int>(i));
      problem->SetString("message",
                         "Not a valid pattern (e.g. example.com, "
                         "*.example.com or example.com/path/*).");
      problems->SetDictionary(problems->GetSize(), problem);
      continue;
    }
    valid.push_back(std::move(pattern));
  }
  if (problems->GetSize() == 0) {
    LinkRoutingConfig config = current;
    SetProfilePatterns(config, profile, valid);
    if (std::optional<Error> error = save(std::move(config))) {
      callback->Failure(kFailed, error->message);
      return;
    }
  }
  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  answer->SetList("problems", problems);
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(answer);
  callback->Success(CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString());
}

}  // namespace brouk
