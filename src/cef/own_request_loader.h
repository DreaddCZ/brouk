#ifndef BROUK_CEF_OWN_REQUEST_LOADER_H_
#define BROUK_CEF_OWN_REQUEST_LOADER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "core/request_log.h"
#include "include/cef_request_context.h"
#include "include/cef_urlrequest.h"

namespace brouk {

// Loads one request of a tab in Brouk itself, so that response header rules
// can change the headers (006; see kMaxOwnResponseBytes for why). Cookies of
// the tab's profile are sent and saved; redirects are not followed but
// returned, so the tab follows them itself. UI thread.
class OwnRequestLoader : public CefURLRequestClient {
 public:
  struct Request {
    std::string method;
    std::string url;
    Headers headers;  // Already without the ones the network stack sets.
    std::string referrer;
    std::optional<std::string> body;
  };
  struct Response {
    int status = 0;
    std::string status_text;
    Headers headers;
    std::string body;  // Decoded.
  };
  // Gets the response, or nullopt when loading failed or the response was
  // larger than kMaxOwnResponseBytes.
  using Done = std::function<void(std::optional<Response>)>;

  static void Start(CefRefPtr<CefRequestContext> context,
                    const Request& request,
                    Done done);

  OwnRequestLoader(const OwnRequestLoader&) = delete;
  OwnRequestLoader& operator=(const OwnRequestLoader&) = delete;

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
  // No login prompt for Brouk's own requests: the request fails.
  bool GetAuthCredentials(bool /*isProxy*/,
                          const CefString& /*host*/,
                          int /*port*/,
                          const CefString& /*realm*/,
                          const CefString& /*scheme*/,
                          CefRefPtr<CefAuthCallback> /*callback*/) override {
    return false;
  }

 private:
  explicit OwnRequestLoader(Done done);

  Done done_;
  CefRefPtr<CefURLRequest> request_;
  std::string body_;
  bool too_large_ = false;

  IMPLEMENT_REFCOUNTING(OwnRequestLoader);
};

}  // namespace brouk

#endif  // BROUK_CEF_OWN_REQUEST_LOADER_H_
