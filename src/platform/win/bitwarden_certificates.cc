#include "platform/win/bitwarden_certificates.h"

#include <windows.h>

#include <wincrypt.h>
#include <winhttp.h>

#include <map>
#include <mutex>
#include <string_view>
#include <system_error>

#include "core/crypto.h"
#include "core/text_file.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr DWORD kTimeoutMs = 15000;

// Closes a WinHTTP handle.
struct HttpHandle {
  HINTERNET handle = nullptr;
  explicit HttpHandle(HINTERNET h) : handle(h) {}
  ~HttpHandle() {
    if (handle) {
      WinHttpCloseHandle(handle);
    }
  }
  HttpHandle(const HttpHandle&) = delete;
  HttpHandle& operator=(const HttpHandle&) = delete;
};

std::string Pem(const CERT_CONTEXT& certificate) {
  const std::string base64 = Base64Encode(
      std::string_view(reinterpret_cast<const char*>(certificate.pbCertEncoded),
                       certificate.cbCertEncoded));
  std::string pem = "-----BEGIN CERTIFICATE-----\n";
  for (std::size_t i = 0; i < base64.size(); i += 64) {
    pem += base64.substr(i, 64) + "\n";
  }
  pem += "-----END CERTIFICATE-----\n";
  return pem;
}

// The authorities above the server's own certificate, as Windows trusts
// them; a single certificate (self-signed, trusted by the user) itself.
// Empty if the server cannot be reached over a trusted connection.
std::string TrustedChainPem(const std::wstring& url) {
  URL_COMPONENTS parts = {};
  parts.dwStructSize = sizeof(parts);
  parts.dwHostNameLength = static_cast<DWORD>(-1);
  parts.dwUrlPathLength = static_cast<DWORD>(-1);
  if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
      parts.nScheme != INTERNET_SCHEME_HTTPS) {
    return {};
  }
  const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
  HttpHandle session(WinHttpOpen(L"Brouk", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                 0));
  if (!session.handle) {
    return {};
  }
  WinHttpSetTimeouts(session.handle, kTimeoutMs, kTimeoutMs, kTimeoutMs,
                     kTimeoutMs);
  HttpHandle connection(
      WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0));
  if (!connection.handle) {
    return {};
  }
  // Only for the certificates: a HEAD request of the server's root.
  HttpHandle request(WinHttpOpenRequest(
      connection.handle, L"HEAD", L"/", nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
  if (!request.handle ||
      !WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request.handle, nullptr)) {
    return {};  // Not reachable, or not trusted by Windows.
  }
  PCCERT_CONTEXT leaf = nullptr;
  DWORD size = sizeof(leaf);
  if (!WinHttpQueryOption(request.handle, WINHTTP_OPTION_SERVER_CERT_CONTEXT,
                          &leaf, &size) ||
      !leaf) {
    return {};
  }
  CERT_CHAIN_PARA para = {};
  para.cbSize = sizeof(para);
  PCCERT_CHAIN_CONTEXT chain = nullptr;
  std::string pem;
  if (CertGetCertificateChain(nullptr, leaf, nullptr, leaf->hCertStore, &para,
                              0, nullptr, &chain) &&
      chain->cChain > 0 &&
      chain->TrustStatus.dwErrorStatus == CERT_TRUST_NO_ERROR) {
    const CERT_SIMPLE_CHAIN& simple = *chain->rgpChain[0];
    for (DWORD i = simple.cElement == 1 ? 0 : 1; i < simple.cElement; ++i) {
      pem += Pem(*simple.rgpElement[i]->pCertContext);
    }
  }
  if (chain) {
    CertFreeCertificateChain(chain);
  }
  CertFreeCertificateContext(leaf);
  return pem;
}

}  // namespace

std::filesystem::path PrepareServerCertificates(
    const std::string& server_url,
    const std::filesystem::path& folder) {
  // Servers already prepared in this run; `mutex` guards `prepared`.
  static std::mutex mutex;
  static std::map<std::string, std::filesystem::path> prepared;
  {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = prepared.find(server_url);
    if (it != prepared.end()) {
      return it->second;
    }
  }
  const std::filesystem::path path = folder / "bitwarden-certificates.pem";
  const std::string pem = TrustedChainPem(Utf8ToWide(server_url));
  if (pem.empty()) {
    // Not reachable now: an earlier copy still helps.
    std::error_code error;
    return std::filesystem::exists(path, error) ? path
                                                : std::filesystem::path();
  }
  if (WriteTextFileAtomically(path, pem).has_value()) {
    return {};
  }
  std::lock_guard<std::mutex> lock(mutex);
  prepared[server_url] = path;
  return path;
}

}  // namespace brouk::win
