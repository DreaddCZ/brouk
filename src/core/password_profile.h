#ifndef BROUK_CORE_PASSWORD_PROFILE_H_
#define BROUK_CORE_PASSWORD_PROFILE_H_

#include <string_view>
#include <vector>

#include "core/crypto.h"
#include "core/password_store.h"
#include "core/result.h"
#include "core/yaml_value.h"

namespace brouk {

// A profile's built-in passwords in its export file (008, extending 005),
// encrypted with a key derived from the export password:
//
//   passwords:
//     kdf: argon2id
//     ops: 3
//     memory: 67108864
//     salt: <base64>
//     data: <base64>     (the entries as JSON, encrypted)
//
// Without the password the file reveals nothing but the number of bytes.
// A profile using Bitwarden exports only its source setting (in the
// settings section), never vault items.
YamlNode ExportProfilePasswords(const std::vector<PasswordEntry>& entries,
                                std::string_view export_password,
                                PasswordKdfCost cost = {});

// Decrypts the section. Fails with a clear message for a wrong password or a
// damaged section.
Result<std::vector<PasswordEntry>> ImportProfilePasswords(
    const YamlNode& node,
    std::string_view export_password);

}  // namespace brouk

#endif  // BROUK_CORE_PASSWORD_PROFILE_H_
