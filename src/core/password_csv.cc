#include "core/password_csv.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>

#include "core/crypto.h"
#include "core/totp.h"
#include "core/web_origin.h"

namespace brouk {

namespace {

std::string Lower(std::string_view text) {
  std::string out(text);
  std::ranges::transform(out, out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

std::string Trim(std::string_view text) {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return std::string(text);
}

// Column positions of the fields we read; -1 when the file has none.
struct Columns {
  std::string format;
  int url = -1;
  int username = -1;
  int password = -1;
  int note = -1;
  int totp = -1;
  int type = -1;     // Bitwarden: only "login" rows.
  int created = -1;  // Firefox: milliseconds since 1970.
};

int IndexOf(const std::vector<std::string>& header, std::string_view name) {
  for (std::size_t i = 0; i < header.size(); ++i) {
    if (header[i] == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

std::optional<Columns> DetectFormat(const std::vector<std::string>& row) {
  std::vector<std::string> header;
  header.reserve(row.size());
  for (const std::string& cell : row) {
    header.push_back(Lower(Trim(cell)));
  }
  Columns columns;
  if (IndexOf(header, "login_uri") >= 0) {
    columns.format = "Bitwarden";
    columns.url = IndexOf(header, "login_uri");
    columns.username = IndexOf(header, "login_username");
    columns.password = IndexOf(header, "login_password");
    columns.note = IndexOf(header, "notes");
    columns.totp = IndexOf(header, "login_totp");
    columns.type = IndexOf(header, "type");
  } else if (IndexOf(header, "formactionorigin") >= 0 ||
             IndexOf(header, "httprealm") >= 0) {
    columns.format = "Firefox";
    columns.url = IndexOf(header, "url");
    columns.username = IndexOf(header, "username");
    columns.password = IndexOf(header, "password");
    columns.created = IndexOf(header, "timecreated");
  } else if (IndexOf(header, "otpauth") >= 0 || IndexOf(header, "title") >= 0) {
    columns.format = "Safari";
    columns.url = IndexOf(header, "url");
    columns.username = IndexOf(header, "username");
    columns.password = IndexOf(header, "password");
    columns.note = IndexOf(header, "notes");
    columns.totp = IndexOf(header, "otpauth");
  } else {
    columns.format = "Chrome";
    columns.url = IndexOf(header, "url");
    columns.username = IndexOf(header, "username");
    columns.password = IndexOf(header, "password");
    columns.note = IndexOf(header, "note");
  }
  if (columns.url < 0 || columns.password < 0) {
    return std::nullopt;
  }
  return columns;
}

std::string Cell(const std::vector<std::string>& row, int index) {
  return index >= 0 && static_cast<std::size_t>(index) < row.size()
             ? row[static_cast<std::size_t>(index)]
             : std::string();
}

std::string CsvField(std::string_view text) {
  if (text.find_first_of(",\"\r\n") == std::string_view::npos) {
    return std::string(text);
  }
  std::string out = "\"";
  for (char c : text) {
    if (c == '"') {
      out += '"';
    }
    out += c;
  }
  out += '"';
  return out;
}

}  // namespace

std::vector<std::vector<std::string>> ParseCsv(std::string_view text) {
  if (text.starts_with("\xEF\xBB\xBF")) {
    text.remove_prefix(3);
  }
  std::vector<std::vector<std::string>> rows;
  std::vector<std::string> row;
  std::string field;
  bool quoted = false;
  bool row_has_content = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (quoted) {
      if (c == '"' && i + 1 < text.size() && text[i + 1] == '"') {
        field += '"';
        ++i;
      } else if (c == '"') {
        quoted = false;
      } else {
        field += c;
      }
      continue;
    }
    if (c == '"') {
      quoted = true;
      row_has_content = true;
    } else if (c == ',') {
      row.push_back(std::move(field));
      field.clear();
      row_has_content = true;
    } else if (c == '\n' || c == '\r') {
      if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
        ++i;
      }
      if (row_has_content || !field.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
      }
      field.clear();
      row.clear();
      row_has_content = false;
    } else {
      field += c;
      row_has_content = true;
    }
  }
  if (row_has_content || !field.empty()) {
    row.push_back(std::move(field));
    rows.push_back(std::move(row));
  }
  return rows;
}

Result<PasswordCsv> ParsePasswordCsv(std::string_view text) {
  std::vector<std::vector<std::string>> rows = ParseCsv(text);
  std::optional<Columns> columns =
      rows.empty() ? std::nullopt : DetectFormat(rows.front());
  if (!columns) {
    return Error{
        "This is not a password export from Chrome, Firefox, Safari or "
        "Bitwarden."};
  }
  PasswordCsv result;
  result.format = columns->format;
  for (std::size_t i = 1; i < rows.size(); ++i) {
    const std::vector<std::string>& row = rows[i];
    std::string label = "Row " + std::to_string(i + 1);
    if (columns->type >= 0 && Lower(Cell(row, columns->type)) != "login") {
      continue;  // Bitwarden notes, cards and identities.
    }
    // Bitwarden puts several addresses into one cell, separated by commas.
    std::vector<std::string> origins;
    std::string urls = Cell(row, columns->url);
    std::size_t start = 0;
    while (start <= urls.size()) {
      std::size_t comma = urls.find(',', start);
      std::string url = Trim(std::string_view(urls).substr(
          start,
          comma == std::string::npos ? std::string::npos : comma - start));
      if (std::optional<std::string> origin = WebOriginOf(url)) {
        if (std::ranges::find(origins, *origin) == origins.end()) {
          origins.push_back(std::move(*origin));
        }
      }
      if (comma == std::string::npos) {
        break;
      }
      start = comma + 1;
    }
    PasswordEntry entry;
    entry.password = Cell(row, columns->password);
    if (origins.empty()) {
      result.problems.push_back(label + ": no website address");
      continue;
    }
    if (entry.password.empty()) {
      result.problems.push_back(label + ": no password");
      continue;
    }
    entry.origin = origins.front();
    entry.other_origins.assign(origins.begin() + 1, origins.end());
    entry.username = Cell(row, columns->username);
    entry.note = Cell(row, columns->note);
    std::string totp = Trim(Cell(row, columns->totp));
    if (!totp.empty()) {
      if (std::optional<TotpSecret> secret = ParseTotpSecret(totp)) {
        entry.totp = TotpSecretToUri(*secret);
      } else {
        result.problems.push_back(label + ": the 2FA secret was not imported");
      }
    }
    std::string created = Cell(row, columns->created);
    std::int64_t millis = 0;
    if (!created.empty() &&
        std::from_chars(created.data(), created.data() + created.size(), millis)
                .ec == std::errc()) {
      entry.created = millis / 1000;
    }
    result.entries.push_back(std::move(entry));
  }
  return result;
}

std::string WritePasswordCsv(const std::vector<PasswordEntry>& entries) {
  std::string out = "name,url,username,password,note\n";
  for (const PasswordEntry& entry : entries) {
    out += CsvField(HostOfOrigin(entry.origin)) + ',' +
           CsvField(entry.origin + "/") + ',' + CsvField(entry.username) + ',' +
           CsvField(entry.password) + ',' + CsvField(entry.note) + '\n';
  }
  return out;
}

PasswordImportPlan PlanPasswordImport(
    const std::vector<PasswordEntry>& existing,
    const std::vector<PasswordEntry>& incoming) {
  PasswordImportPlan plan;
  auto same_login = [](const PasswordEntry& a, const PasswordEntry& b) {
    return a.origin == b.origin && a.username == b.username;
  };
  for (const PasswordEntry& entry : incoming) {
    auto matches = [&](const PasswordEntry& other) {
      return same_login(entry, other);
    };
    auto old = std::ranges::find_if(existing, matches);
    auto earlier = std::ranges::find_if(plan.to_add, matches);
    const PasswordEntry* found = old != existing.end()          ? &*old
                                 : earlier != plan.to_add.end() ? &*earlier
                                                                : nullptr;
    if (found == nullptr) {
      plan.to_add.push_back(entry);
    } else if (SecretsEqual(found->password, entry.password)) {
      ++plan.skipped;
    } else {
      plan.conflicts.push_back(entry);
    }
  }
  return plan;
}

}  // namespace brouk
