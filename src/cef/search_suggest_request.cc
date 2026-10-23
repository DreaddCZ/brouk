#include "cef/search_suggest_request.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Real answers are a few hundred bytes.
constexpr std::size_t kMaxAnswerBytes = std::size_t{64} * 1024;
constexpr std::size_t kMaxSuggestions = 20;
constexpr std::size_t kMaxSuggestionLength = 512;

}  // namespace

std::vector<std::string> ParseOpenSearchSuggestions(const std::string& body) {
  std::vector<std::string> suggestions;
  CefRefPtr<CefValue> value = CefParseJSON(body, JSON_PARSER_RFC);
  if (!value || value->GetType() != VTYPE_LIST) {
    return suggestions;
  }
  CefRefPtr<CefListValue> answer = value->GetList();
  if (answer->GetSize() < 2 || answer->GetType(1) != VTYPE_LIST) {
    return suggestions;
  }
  CefRefPtr<CefListValue> list = answer->GetList(1);
  for (std::size_t i = 0; i < list->GetSize() && i < kMaxSuggestions; ++i) {
    if (list->GetType(i) != VTYPE_STRING) {
      continue;
    }
    std::string text = list->GetString(i).ToString();
    if (!text.empty() && text.size() <= kMaxSuggestionLength) {
      suggestions.push_back(std::move(text));
    }
  }
  return suggestions;
}

SearchSuggestRequest::SearchSuggestRequest(Done done)
    : done_(std::move(done)) {}

// static
CefRefPtr<SearchSuggestRequest> SearchSuggestRequest::Start(
    CefRefPtr<CefRequestContext> context,
    const std::string& url,
    Done done) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefRequest> request = CefRequest::Create();
  request->SetURL(url);
  request->SetMethod("GET");
  request->SetFlags(UR_FLAG_ALLOW_STORED_CREDENTIALS | UR_FLAG_NO_RETRY_ON_5XX);
  CefRefPtr<SearchSuggestRequest> client(
      new SearchSuggestRequest(std::move(done)));
  client->request_ = CefURLRequest::Create(request, client, std::move(context));
  return client;
}

void SearchSuggestRequest::Cancel() {
  CEF_REQUIRE_UI_THREAD();
  done_ = nullptr;
  if (request_) {
    request_->Cancel();
    request_ = nullptr;
  }
}

void SearchSuggestRequest::OnDownloadData(CefRefPtr<CefURLRequest> request,
                                          const void* data,
                                          size_t data_length) {
  CEF_REQUIRE_UI_THREAD();
  if (body_.size() + data_length > kMaxAnswerBytes) {
    body_.clear();
    done_ = nullptr;
    request->Cancel();
    return;
  }
  body_.append(static_cast<const char*>(data), data_length);
}

void SearchSuggestRequest::OnRequestComplete(CefRefPtr<CefURLRequest> request) {
  CEF_REQUIRE_UI_THREAD();
  Done done = std::move(done_);
  done_ = nullptr;
  request_ = nullptr;
  if (!done) {
    return;
  }
  CefRefPtr<CefResponse> response = request->GetResponse();
  if (request->GetRequestStatus() != UR_SUCCESS || !response ||
      response->GetStatus() != 200) {
    done({});
    return;
  }
  done(ParseOpenSearchSuggestions(body_));
}

}  // namespace brouk
