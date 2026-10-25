#ifndef BROUK_CORE_PASSWORD_CSV_H_
#define BROUK_CORE_PASSWORD_CSV_H_

#include <string>
#include <string_view>
#include <vector>

#include "core/password_store.h"
#include "core/result.h"

namespace brouk {

// Importing and exporting passwords as CSV files (008).

// Rows of a CSV file (RFC 4180: quoted fields, "" for a quote, CRLF or LF).
// A UTF-8 byte order mark is ignored.
std::vector<std::vector<std::string>> ParseCsv(std::string_view text);

struct PasswordCsv {
  // "Chrome", "Firefox", "Safari" or "Bitwarden", from the header row.
  std::string format;
  std::vector<PasswordEntry> entries;
  // Rows that were not imported, for the summary ("Row 7: not a website").
  std::vector<std::string> problems;
};

// Reads an export of Chrome, Firefox, Safari or Bitwarden. Fails if the
// header row is not one of theirs. Rows without a web address or password
// are left out and listed in `problems`.
Result<PasswordCsv> ParsePasswordCsv(std::string_view text);

// Chrome's format: name,url,username,password,note.
std::string WritePasswordCsv(const std::vector<PasswordEntry>& entries);

// What importing `incoming` into a profile with `existing` entries does:
// exact duplicates (same origin, user name and password) are skipped;
// entries with the same origin and user name but another password are
// conflicts, not imported but listed so the user can decide.
struct PasswordImportPlan {
  std::vector<PasswordEntry> to_add;
  int skipped = 0;
  std::vector<PasswordEntry> conflicts;
};
PasswordImportPlan PlanPasswordImport(
    const std::vector<PasswordEntry>& existing,
    const std::vector<PasswordEntry>& incoming);

}  // namespace brouk

#endif  // BROUK_CORE_PASSWORD_CSV_H_
