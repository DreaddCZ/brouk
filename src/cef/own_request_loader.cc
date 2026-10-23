#include "cef/own_request_loader.h"

#include <utility>

#include "core/request_rules.h"
#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

OwnRequestLoader::OwnRequestLoader(Done done) : done_(std::move(done)) {}

// static
void OwnRequestLoader::Start(CefRefPtr<CefRequestContext> context,
                             const Request& request,
                             Done done) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefRequest> cef_request = CefRequest::Create();
  cef_request->SetURL(request.url);
  cef_request->SetMethod(request.method);
  CefRequest::HeaderMap headers;
  for (const auto& [name, value] : request.headers) {
    headers.emplace(name, value);
  }
  cef_request->SetHeaderMap(headers);
  if (!request.referrer.empty()) {
    // The page's referrer, already reduced by its policy.
    cef_request->SetReferrer(request.referrer,
                             REFERRER_POLICY_NEVER_CLEAR_REFERRER);
  }
  if (request.body) {
    CefRefPtr<CefPostDataElement> element = CefPostDataElement::Create();
    element->SetToBytes(request.body->size(), request.body->data());
    CefRefPtr<CefPostData> post_data = CefPostData::Create();
    post_data->AddElement(element);
    cef_request->SetPostData(post_data);
  }
  cef_request->SetFlags(UR_FLAG_ALLOW_STORED_CREDENTIALS |
                        UR_FLAG_STOP_ON_REDIRECT | UR_FLAG_NO_RETRY_ON_5XX);
  CefRefPtr<OwnRequestLoader> loader(new OwnRequestLoader(std::move(done)));
  CefRefPtr<CefURLRequest> url_request =
      CefURLRequest::Create(cef_request, loader, std::move(context));
  if (!loader->done_) {
    return;  // Already completed.
  }
  if (!url_request) {
    Done failed = std::move(loader->done_);
    loader->done_ = nullptr;
    failed(std::nullopt);
    return;
  }
  // Kept until it completes (the request holds the loader as its client).
  loader->request_ = url_request;
}

void OwnRequestLoader::OnDownloadData(CefRefPtr<CefURLRequest> request,
                                      const void* data,
                                      size_t data_length) {
  CEF_REQUIRE_UI_THREAD();
  if (too_large_) {
    return;
  }
  if (body_.size() + data_length > kMaxOwnResponseBytes) {
    too_large_ = true;
    body_.clear();
    // Not from inside the request's own callback.
    CefPostTask(TID_UI, base::BindOnce(&CefURLRequest::Cancel, request));
    return;
  }
  body_.append(static_cast<const char*>(data), data_length);
}

void OwnRequestLoader::OnRequestComplete(CefRefPtr<CefURLRequest> request) {
  CEF_REQUIRE_UI_THREAD();
  if (!done_) {
    return;
  }
  Done done = std::move(done_);
  done_ = nullptr;
  request_ = nullptr;
  CefRefPtr<CefResponse> response = request->GetResponse();
  int status = response ? response->GetStatus() : 0;
  CefURLRequest::Status result = request->GetRequestStatus();
  // UR_FLAG_STOP_ON_REDIRECT ends a redirect as canceled, with its headers.
  bool redirect = result == UR_CANCELED && !too_large_ && status >= 300 &&
                  status < 400 &&
                  !response->GetHeaderByName("Location").empty();
  if (too_large_ || status == 0 || (result != UR_SUCCESS && !redirect)) {
    done(std::nullopt);
    return;
  }
  Response answer;
  answer.status = status;
  answer.status_text = response->GetStatusText().ToString();
  CefResponse::HeaderMap headers;
  response->GetHeaderMap(headers);
  for (const auto& [name, value] : headers) {
    answer.headers.emplace_back(name.ToString(), value.ToString());
  }
  if (!redirect) {
    answer.body = std::move(body_);
  }
  done(std::move(answer));
}

}  // namespace brouk
