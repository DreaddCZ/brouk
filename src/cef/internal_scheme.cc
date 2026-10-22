#include "cef/internal_scheme.h"

#include <optional>
#include <string>
#include <utility>

#include "core/internal_pages.h"
#include "include/cef_stream.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_stream_resource_handler.h"

namespace brouk {

namespace {

// IO thread. Maps a request to a file of the web folder; anything not
// allowed by InternalResourcePath() gets a 404.
class InternalSchemeFactory : public CefSchemeHandlerFactory {
 public:
  explicit InternalSchemeFactory(std::string web_dir)
      : web_dir_(std::move(web_dir)) {}
  InternalSchemeFactory(const InternalSchemeFactory&) = delete;
  InternalSchemeFactory& operator=(const InternalSchemeFactory&) = delete;

  CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> /*browser*/,
                                       CefRefPtr<CefFrame> /*frame*/,
                                       const CefString& /*scheme_name*/,
                                       CefRefPtr<CefRequest> request) override {
    CEF_REQUIRE_IO_THREAD();
    std::optional<std::string> path =
        InternalResourcePath(request->GetURL().ToString());
    CefRefPtr<CefStreamReader> reader =
        path ? CefStreamReader::CreateForFile(web_dir_ + "/" + *path) : nullptr;
    CefResponse::HeaderMap headers;
    headers.emplace("Content-Security-Policy", std::string(kInternalPageCsp));
    headers.emplace("X-Content-Type-Options", "nosniff");
    headers.emplace("Cache-Control", "no-store");
    if (!path || !reader) {
      std::string text = "Not found";
      return new CefStreamResourceHandler(
          404, "Not Found", "text/plain", headers,
          CefStreamReader::CreateForData(text.data(), text.size()));
    }
    return new CefStreamResourceHandler(
        200, "OK", std::string(InternalResourceMimeType(*path)), headers,
        reader);
  }

 private:
  const std::string web_dir_;
  IMPLEMENT_REFCOUNTING(InternalSchemeFactory);
};

}  // namespace

void InstallInternalPages(const CefRefPtr<CefRequestContext>& context,
                          const std::string& web_dir) {
  CEF_REQUIRE_UI_THREAD();
  context->RegisterSchemeHandlerFactory(std::string(kInternalScheme), "",
                                        new InternalSchemeFactory(web_dir));
}

}  // namespace brouk
