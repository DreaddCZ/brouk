#include "cef/breach_check.h"

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include "cef/own_request_loader.h"
#include "core/crypto.h"
#include "core/password_checkup.h"
#include "include/cef_request_context.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

struct Check {
  std::vector<PwnedQuery> queries;
  std::map<std::string, std::string> answers;  // By prefix.
  std::size_t pending = 0;
  BreachDone done;

  void Finish() {
    std::vector<std::int64_t> counts;
    counts.reserve(queries.size());
    for (const PwnedQuery& query : queries) {
      auto answer = answers.find(query.prefix);
      counts.push_back(answer == answers.end()
                           ? 0
                           : PwnedCount(answer->second, query.suffix));
    }
    done(std::move(counts));
  }
};

}  // namespace

void CheckBreaches(std::vector<std::string> passwords, BreachDone done) {
  CEF_REQUIRE_UI_THREAD();
  auto check = std::make_shared<Check>();
  check->done = std::move(done);
  for (std::string& password : passwords) {
    check->queries.push_back(MakePwnedQuery(password));
    Wipe(password);
  }
  std::map<std::string, bool> prefixes;
  for (const PwnedQuery& query : check->queries) {
    prefixes.emplace(query.prefix, true);
  }
  check->pending = prefixes.size();
  if (check->pending == 0) {
    check->Finish();
    return;
  }
  for (const auto& [prefix, unused] : prefixes) {
    OwnRequestLoader::Request request;
    request.method = "GET";
    request.url = std::string(kPwnedPasswordsUrl) + prefix;
    request.headers = {{"Add-Padding", "true"}};
    std::string key = prefix;
    OwnRequestLoader::Start(
        CefRequestContext::GetGlobalContext(), request,
        [check, key](std::optional<OwnRequestLoader::Response> response) {
          if (response && response->status == 200) {
            check->answers[key] = std::move(response->body);
          }
          if (--check->pending == 0) {
            check->Finish();
          }
        });
  }
}

}  // namespace brouk
