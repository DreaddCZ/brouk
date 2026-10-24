#include "cef/adblock_request_handler.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/adblock_policy.h"
#include "core/url_pattern.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_stream_resource_handler.h"

namespace brouk {

namespace {

// Cancels, redirects or rewrites one request. IO thread.
class AdblockResourceRequestHandler : public CefResourceRequestHandler {
 public:
  enum class Mode { kCancel, kRedirect, kRewrite };

  AdblockResourceRequestHandler(Mode mode, std::string value)
      : mode_(mode), value_(std::move(value)) {}

  ReturnValue OnBeforeResourceLoad(
      CefRefPtr<CefBrowser> /*browser*/,
      CefRefPtr<CefFrame> /*frame*/,
      CefRefPtr<CefRequest> request,
      CefRefPtr<CefCallback> /*callback*/) override {
    CEF_REQUIRE_IO_THREAD();
    switch (mode_) {
      case Mode::kCancel:
        return RV_CANCEL;
      case Mode::kRewrite:
        request->SetURL(value_);
        return RV_CONTINUE;
      case Mode::kRedirect:
        return RV_CONTINUE;
    }
    return RV_CONTINUE;
  }

  CefRefPtr<CefResourceHandler> GetResourceHandler(
      CefRefPtr<CefBrowser> /*browser*/,
      CefRefPtr<CefFrame> /*frame*/,
      CefRefPtr<CefRequest> /*request*/) override {
    CEF_REQUIRE_IO_THREAD();
    if (mode_ != Mode::kRedirect) {
      return nullptr;
    }
    std::optional<DataUrl> data = ParseDataUrl(value_);
    if (!data) {
      return nullptr;
    }
    body_ = std::move(data->body);
    // A neutral resource of the same kind (an empty script, a 1×1 image…).
    return new CefStreamResourceHandler(
        data->mime_type,
        CefStreamReader::CreateForData(body_.data(), body_.size()));
  }

 private:
  Mode mode_;
  std::string value_;
  std::string body_;  // Kept alive for the stream reader.
  IMPLEMENT_REFCOUNTING(AdblockResourceRequestHandler);
};

}  // namespace

std::string_view AdblockRequestType(cef_resource_type_t type) {
  switch (type) {
    case RT_MAIN_FRAME:
      return "document";
    case RT_SUB_FRAME:
      return "subdocument";
    case RT_STYLESHEET:
      return "stylesheet";
    case RT_SCRIPT:
    case RT_WORKER:
    case RT_SHARED_WORKER:
    case RT_SERVICE_WORKER:
      return "script";
    case RT_IMAGE:
    case RT_FAVICON:
      return "image";
    case RT_FONT_RESOURCE:
      return "font";
    case RT_OBJECT:
    case RT_PLUGIN_RESOURCE:
      return "object";
    case RT_MEDIA:
      return "media";
    case RT_XHR:
      return "xmlhttprequest";
    case RT_PING:
      return "ping";
    case RT_CSP_REPORT:
      return "csp_report";
    default:
      return "other";
  }
}

CefRefPtr<CefResourceRequestHandler> CheckAdblockRequest(
    const AdblockService& service,
    const ProfileId& profile,
    const std::string& url,
    const std::string& page_url,
    cef_resource_type_t type,
    AdblockRequestInfo* info) {
  CEF_REQUIRE_IO_THREAD();
  *info = {};
  if (!url.starts_with("http://") && !url.starts_with("https://") &&
      !url.starts_with("ws://") && !url.starts_with("wss://")) {
    return nullptr;
  }
  std::shared_ptr<const ProfileAdblockPolicy> policy =
      service.PolicyFor(profile);
  if (!policy || !policy->enabled ||
      MatchesAnyPattern(policy->allowlist, page_url)) {
    return nullptr;
  }
  std::shared_ptr<const ActiveAdblockEngine> engine =
      service.EngineFor(policy->categories);
  if (!engine) {
    return nullptr;
  }
  std::string_view request_type = AdblockRequestType(type);
  AdblockMatch match = engine->engine->Check(url, page_url, request_type);
  std::string host = HostOfUrl(url);
  AdblockDecision decision =
      DecideRequest(match, host, *service.ProtectedHosts());

  info->url = url;
  info->host = host;
  info->type = std::string(request_type);
  info->main_frame = type == RT_MAIN_FRAME;
  info->third_party = IsThirdParty(host, HostOfUrl(page_url));
  info->blocked = decision.block;
  info->excepted = decision.excepted;
  const std::optional<AdblockRule>& rule =
      decision.block ? match.filter
                     : (match.exception ? match.exception : match.filter);
  if (rule && (decision.block || decision.excepted)) {
    info->rule = rule->text;
    if (rule->list >= 0 &&
        static_cast<std::size_t>(rule->list) < engine->list_ids.size()) {
      info->list_id = engine->list_ids[static_cast<std::size_t>(rule->list)];
    }
  }
  using Mode = AdblockResourceRequestHandler::Mode;
  if (decision.block) {
    // Pages and frames are not replaced by a redirect resource.
    if (!decision.redirect.empty() && type != RT_MAIN_FRAME &&
        type != RT_SUB_FRAME) {
      return new AdblockResourceRequestHandler(Mode::kRedirect,
                                               decision.redirect);
    }
    return new AdblockResourceRequestHandler(Mode::kCancel, "");
  }
  if (!decision.rewritten_url.empty() && decision.rewritten_url != url) {
    return new AdblockResourceRequestHandler(Mode::kRewrite,
                                             decision.rewritten_url);
  }
  return nullptr;
}

}  // namespace brouk
