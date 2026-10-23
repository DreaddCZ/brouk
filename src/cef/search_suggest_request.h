#ifndef BROUK_CEF_SEARCH_SUGGEST_REQUEST_H_
#define BROUK_CEF_SEARCH_SUGGEST_REQUEST_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "include/cef_request_context.h"
#include "include/cef_urlrequest.h"

namespace brouk {

// Asks the search engine for suggestions while the user types (003). The
// answer is OpenSearch JSON: ["text", ["suggestion", …], …]. UI thread.
class SearchSuggestRequest : public CefURLRequestClient {
 public:
  // Gets the suggestions; empty when the request failed. Not called after
  // Cancel().
  using Done = std::function<void(std::vector<std::string>)>;

  // Starts a GET of `url` in the tab's profile (its cookies, as Chrome's
  // suggestions use).
  static CefRefPtr<SearchSuggestRequest> Start(
      CefRefPtr<CefRequestContext> context,
      const std::string& url,
      Done done);
  // A newer request replaces this one.
  void Cancel();

  SearchSuggestRequest(const SearchSuggestRequest&) = delete;
  SearchSuggestRequest& operator=(const SearchSuggestRequest&) = delete;

  // CefURLRequestClient:
  void OnRequestComplete(CefRefPtr<CefURLRequest> request) override;
  void OnUploadProgress(CefRefPtr<CefURLRequest> /*request*/,
                        int64_t /*current*/,
                        int64_t /*total*/) override {}
  void OnDownloadProgress(CefRefPtr<CefURLRequest> /*request*/,
                          int64_t /*current*/,
                          int64_t /*total*/) override {}
  void OnDownloadData(CefRefPtr<CefURLRequest> request,
                      const void* data,
                      size_t data_length) override;
  bool GetAuthCredentials(bool /*isProxy*/,
                          const CefString& /*host*/,
                          int /*port*/,
                          const CefString& /*realm*/,
                          const CefString& /*scheme*/,
                          CefRefPtr<CefAuthCallback> /*callback*/) override {
    return false;
  }

 private:
  explicit SearchSuggestRequest(Done done);

  Done done_;
  CefRefPtr<CefURLRequest> request_;
  std::string body_;

  IMPLEMENT_REFCOUNTING(SearchSuggestRequest);
};

// The suggestions in an OpenSearch answer (untrusted: from the network);
// empty if it is not one.
std::vector<std::string> ParseOpenSearchSuggestions(const std::string& body);

}  // namespace brouk

#endif  // BROUK_CEF_SEARCH_SUGGEST_REQUEST_H_
