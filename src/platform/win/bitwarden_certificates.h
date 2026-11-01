#ifndef BROUK_PLATFORM_WIN_BITWARDEN_CERTIFICATES_H_
#define BROUK_PLATFORM_WIN_BITWARDEN_CERTIFICATES_H_

#include <filesystem>
#include <string>

namespace brouk::win {

// The certificate authorities that Windows trusts for `server_url`, written
// to a PEM file in `folder` for the Bitwarden CLI (008): `bw` is a Node.js
// program, which does not use the Windows certificate store, so a company's
// own certificate authority is unknown to it. Connects to the server once
// per run and server; returns "" if the server could not be reached or is
// not trusted (an earlier file is still used then). Blocks: never on the UI
// thread. The macOS counterpart is PrepareServerCertificates.
std::filesystem::path PrepareServerCertificates(
    const std::string& server_url,
    const std::filesystem::path& folder);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_BITWARDEN_CERTIFICATES_H_
