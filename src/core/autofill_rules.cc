#include "core/autofill_rules.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <utility>

namespace brouk {

namespace {

// Decodes one UTF-8 code point at `i` (advancing it); U+FFFD for bad bytes.
char32_t NextCodePoint(std::string_view text, std::size_t& i) {
  auto byte = [&](std::size_t k) {
    return static_cast<unsigned char>(text[k]);
  };
  unsigned char first = byte(i);
  if (first < 0x80) {
    ++i;
    return first;
  }
  int length = first >= 0xF0 ? 4 : first >= 0xE0 ? 3 : first >= 0xC0 ? 2 : 0;
  if (length == 0 || i + static_cast<std::size_t>(length) > text.size()) {
    ++i;
    return 0xFFFD;
  }
  char32_t code = first & (0x7F >> length);
  for (int k = 1; k < length; ++k) {
    unsigned char next = byte(i + static_cast<std::size_t>(k));
    if ((next & 0xC0) != 0x80) {
      ++i;
      return 0xFFFD;
    }
    code = (code << 6) | (next & 0x3F);
  }
  i += static_cast<std::size_t>(length);
  return code;
}

// The base letter of a Latin letter with accents (Czech, Slovak, German,
// Polish, French…), lower case; 0 if it is not one.
char FoldLatin(char32_t c) {
  struct Fold {
    char32_t from;
    char to;
  };
  static constexpr std::array<Fold, 62> kFolds = {{
      {U'á', 'a'}, {U'ä', 'a'}, {U'à', 'a'}, {U'â', 'a'}, {U'ą', 'a'},
      {U'č', 'c'}, {U'ć', 'c'}, {U'ç', 'c'}, {U'ď', 'd'}, {U'é', 'e'},
      {U'ě', 'e'}, {U'ë', 'e'}, {U'è', 'e'}, {U'ê', 'e'}, {U'ę', 'e'},
      {U'í', 'i'}, {U'ï', 'i'}, {U'î', 'i'}, {U'ľ', 'l'}, {U'ĺ', 'l'},
      {U'ł', 'l'}, {U'ň', 'n'}, {U'ń', 'n'}, {U'ñ', 'n'}, {U'ó', 'o'},
      {U'ö', 'o'}, {U'ô', 'o'}, {U'ő', 'o'}, {U'ř', 'r'}, {U'ŕ', 'r'},
      {U'š', 's'}, {U'ś', 's'}, {U'ß', 's'}, {U'ť', 't'}, {U'ú', 'u'},
      {U'ů', 'u'}, {U'ü', 'u'}, {U'ű', 'u'}, {U'ý', 'y'}, {U'ž', 'z'},
      {U'ź', 'z'}, {U'ż', 'z'}, {U'Á', 'a'}, {U'Ä', 'a'}, {U'Č', 'c'},
      {U'Ď', 'd'}, {U'É', 'e'}, {U'Ě', 'e'}, {U'Í', 'i'}, {U'Ľ', 'l'},
      {U'Ł', 'l'}, {U'Ň', 'n'}, {U'Ó', 'o'}, {U'Ö', 'o'}, {U'Ř', 'r'},
      {U'Š', 's'}, {U'Ť', 't'}, {U'Ú', 'u'}, {U'Ů', 'u'}, {U'Ü', 'u'},
      {U'Ý', 'y'}, {U'Ž', 'z'},
  }};
  for (const Fold& fold : kFolds) {
    if (fold.from == c) {
      return fold.to;
    }
  }
  return 0;
}

void AppendUtf8(std::string& out, char32_t c) {
  if (c < 0x80) {
    out.push_back(static_cast<char>(c));
  } else if (c < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (c >> 6)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else if (c < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (c >> 12)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (c >> 18)));
    out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  }
}

bool IsDigits(std::string_view text) {
  return !text.empty() &&
         std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

// A VAT ID body against a pattern: d digit, a letter, x letter or digit,
// other characters literally.
bool MatchesPattern(std::string_view text, std::string_view pattern) {
  if (text.size() != pattern.size()) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    bool digit = c >= '0' && c <= '9';
    bool letter = c >= 'A' && c <= 'Z';
    switch (pattern[i]) {
      case 'd':
        if (!digit) {
          return false;
        }
        break;
      case 'a':
        if (!letter) {
          return false;
        }
        break;
      case 'x':
        if (!digit && !letter) {
          return false;
        }
        break;
      default:
        if (c != pattern[i]) {
          return false;
        }
    }
  }
  return true;
}

struct VatFormat {
  std::string_view country;
  std::array<std::string_view, 3> patterns;
};

// EU VAT ID formats (VIES), without the country prefix.
constexpr std::array<VatFormat, 28> kVatFormats = {{
    {"AT", {"Udddddddd", "", ""}},
    {"BE", {"dddddddddd", "", ""}},
    {"BG", {"ddddddddd", "dddddddddd", ""}},
    {"CY", {"dddddddda", "", ""}},
    {"CZ", {"dddddddd", "ddddddddd", "dddddddddd"}},
    {"DE", {"ddddddddd", "", ""}},
    {"DK", {"dddddddd", "", ""}},
    {"EE", {"ddddddddd", "", ""}},
    {"EL", {"ddddddddd", "", ""}},
    {"ES", {"xdddddddx", "", ""}},
    {"FI", {"dddddddd", "", ""}},
    {"FR", {"xxddddddddd", "", ""}},
    {"HR", {"ddddddddddd", "", ""}},
    {"HU", {"dddddddd", "", ""}},
    {"IE", {"dxdddddA", "dxdddddAA", "dddddddAA"}},
    {"IT", {"ddddddddddd", "", ""}},
    {"LT", {"ddddddddd", "dddddddddddd", ""}},
    {"LU", {"dddddddd", "", ""}},
    {"LV", {"ddddddddddd", "", ""}},
    {"MT", {"dddddddd", "", ""}},
    {"NL", {"dddddddddBdd", "", ""}},
    {"PL", {"dddddddddd", "", ""}},
    {"PT", {"ddddddddd", "", ""}},
    {"RO", {"dd", "dddddddddd", ""}},
    {"SE", {"dddddddddddd", "", ""}},
    {"SI", {"dddddddd", "", ""}},
    {"SK", {"dddddddddd", "", ""}},
    {"XI", {"ddddddddd", "dddddddddddd", ""}},
}};

bool ValidVatId(const std::string& vat) {
  if (vat.size() < 4) {
    return false;
  }
  std::string country = vat.substr(0, 2);
  std::string body = vat.substr(2);
  for (const VatFormat& format : kVatFormats) {
    if (format.country != country) {
      continue;
    }
    if (country == "RO") {
      // Romania: 2 to 10 digits.
      return IsDigits(body) && body.size() >= 2 && body.size() <= 10;
    }
    for (std::string_view pattern : format.patterns) {
      std::string upper(pattern);
      // 'A' in the Irish patterns means a letter.
      std::ranges::replace(upper, 'A', 'a');
      if (!pattern.empty() && MatchesPattern(body, upper)) {
        return true;
      }
    }
    return false;
  }
  return false;
}

bool ValidIco(const std::string& ico) {
  if (!IsDigits(ico) || ico.size() > 8) {
    return false;
  }
  std::string padded = std::string(8 - ico.size(), '0') + ico;
  int sum = 0;
  for (int i = 0; i < 7; ++i) {
    sum += (padded[static_cast<std::size_t>(i)] - '0') * (8 - i);
  }
  int check = (11 - (sum % 11)) % 10;
  return padded[7] - '0' == check;
}

bool ValidIban(const std::string& iban) {
  if (iban.size() < 15 || iban.size() > 34 || !std::isalpha(iban[0]) ||
      !std::isalpha(iban[1]) || !std::isdigit(iban[2]) ||
      !std::isdigit(iban[3])) {
    return false;
  }
  std::string rearranged = iban.substr(4) + iban.substr(0, 4);
  int remainder = 0;
  for (char c : rearranged) {
    if (c >= '0' && c <= '9') {
      remainder = (remainder * 10 + (c - '0')) % 97;
    } else if (c >= 'A' && c <= 'Z') {
      int value = c - 'A' + 10;
      remainder = (remainder * 100 + value) % 97;
    } else {
      return false;
    }
  }
  return remainder == 1;
}

bool IsNameOrContact(AutofillField field) {
  return field == AutofillField::kFirstName ||
         field == AutofillField::kLastName || field == AutofillField::kEmail ||
         field == AutofillField::kPhone || field == AutofillField::kStreet;
}

struct Pair {
  std::string submitted;
  std::string stored;
};

}  // namespace

std::string NormalizeText(std::string_view text) {
  std::string out;
  bool space = false;
  std::size_t i = 0;
  while (i < text.size()) {
    char32_t c = NextCodePoint(text, i);
    char folded = FoldLatin(c);
    if (folded != 0) {
      c = static_cast<unsigned char>(folded);
    }
    if (c < 0x80) {
      auto ascii = static_cast<unsigned char>(c);
      if (std::isalnum(ascii) != 0) {
        if (space && !out.empty()) {
          out.push_back(' ');
        }
        space = false;
        out.push_back(static_cast<char>(std::tolower(ascii)));
      } else {
        // Spaces and punctuation separate words but are not compared.
        space = true;
      }
      continue;
    }
    if (space && !out.empty()) {
      out.push_back(' ');
    }
    space = false;
    AppendUtf8(out, c);
  }
  return out;
}

std::string_view CountryCallingCode(std::string_view region) {
  struct Code {
    std::string_view region;
    std::string_view code;
  };
  static constexpr std::array<Code, 34> kCodes = {{
      {"CZ", "420"}, {"SK", "421"}, {"DE", "49"},  {"AT", "43"},  {"PL", "48"},
      {"HU", "36"},  {"GB", "44"},  {"IE", "353"}, {"FR", "33"},  {"BE", "32"},
      {"NL", "31"},  {"LU", "352"}, {"CH", "41"},  {"IT", "39"},  {"ES", "34"},
      {"PT", "351"}, {"DK", "45"},  {"SE", "46"},  {"NO", "47"},  {"FI", "358"},
      {"EE", "372"}, {"LV", "371"}, {"LT", "370"}, {"SI", "386"}, {"HR", "385"},
      {"RO", "40"},  {"BG", "359"}, {"GR", "30"},  {"CY", "357"}, {"MT", "356"},
      {"US", "1"},   {"CA", "1"},   {"UA", "380"}, {"IS", "354"},
  }};
  for (const Code& code : kCodes) {
    if (code.region == region) {
      return code.code;
    }
  }
  return {};
}

std::string NormalizePhone(std::string_view text, std::string_view country) {
  std::string digits;
  bool plus = false;
  for (char c : text) {
    if (c == '+' && digits.empty()) {
      plus = true;
    } else if (c >= '0' && c <= '9') {
      digits.push_back(c);
    }
  }
  if (!plus && digits.starts_with("00")) {
    plus = true;
    digits.erase(0, 2);
  }
  if (plus) {
    return "+" + digits;
  }
  std::string_view code = CountryCallingCode(country);
  if (code.empty() || digits.empty()) {
    return digits;
  }
  // A national trunk prefix (0 in most of Europe; not in CZ and SK).
  if (digits.starts_with('0') && country != "IT") {
    digits.erase(0, 1);
  }
  return "+" + std::string(code) + digits;
}

std::string NormalizeCompact(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '-' || c == '.') {
      continue;
    }
    out.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

std::string NormalizeValue(AutofillField field,
                           std::string_view value,
                           std::string_view country) {
  switch (field) {
    case AutofillField::kPhone:
      return NormalizePhone(value, country);
    case AutofillField::kPostalCode:
    case AutofillField::kIco:
    case AutofillField::kDic:
    case AutofillField::kVatId:
    case AutofillField::kIban:
    case AutofillField::kBic:
      return NormalizeCompact(value);
    case AutofillField::kEmail: {
      std::string email;
      for (char c : value) {
        if (c != ' ' && c != '\t' && c != '\n') {
          email.push_back(c);
        }
      }
      std::ranges::transform(email, email.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
      });
      return email;
    }
    default:
      return NormalizeText(value);
  }
}

std::size_t EditDistance(std::string_view a,
                         std::string_view b,
                         std::size_t limit) {
  if (a.size() > b.size()) {
    std::swap(a, b);
  }
  if (b.size() - a.size() > limit) {
    return limit + 1;
  }
  std::vector<std::size_t> previous(a.size() + 1);
  std::vector<std::size_t> current(a.size() + 1);
  for (std::size_t i = 0; i <= a.size(); ++i) {
    previous[i] = i;
  }
  for (std::size_t j = 1; j <= b.size(); ++j) {
    current[0] = j;
    std::size_t row_min = current[0];
    for (std::size_t i = 1; i <= a.size(); ++i) {
      std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
      current[i] = std::min(
          {previous[i] + 1, current[i - 1] + 1, previous[i - 1] + cost});
      row_min = std::min(row_min, current[i]);
    }
    if (row_min > limit) {
      return limit + 1;
    }
    std::swap(previous, current);
  }
  return std::min(previous[a.size()], limit + 1);
}

AutofillComparison CompareWithEntries(const AutofillValues& submitted,
                                      const std::vector<AutofillEntry>& entries,
                                      std::string_view country) {
  AutofillComparison best;
  int best_rank = 0;  // 3 same, 2 adds, 1 similar.
  std::size_t best_overlap = 0;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    const AutofillEntry& entry = entries[index];
    std::vector<AutofillField> new_fields;
    std::vector<AutofillField> changed;
    std::size_t overlap = 0;
    std::map<AutofillField, Pair> pairs;
    for (const auto& [field, value] : submitted) {
      std::string mine = NormalizeValue(field, value, country);
      if (mine.empty()) {
        continue;
      }
      std::string theirs = NormalizeValue(field, entry.Get(field), country);
      if (theirs.empty()) {
        new_fields.push_back(field);
        continue;
      }
      ++overlap;
      pairs[field] = {mine, theirs};
      if (mine != theirs) {
        changed.push_back(field);
      }
    }
    int rank = 0;
    if (overlap > 0 && changed.empty()) {
      rank = new_fields.empty() ? 3 : 2;
    } else if (overlap > 0) {
      auto same = [&](AutofillField field) {
        auto it = pairs.find(field);
        return it != pairs.end() && it->second.submitted == it->second.stored;
      };
      auto near = [&](AutofillField field) {
        auto it = pairs.find(field);
        return it != pairs.end() &&
               EditDistance(it->second.submitted, it->second.stored, 2) <= 2;
      };
      bool name_near = near(AutofillField::kLastName) &&
                       (near(AutofillField::kFirstName) ||
                        !pairs.contains(AutofillField::kFirstName));
      if (same(AutofillField::kEmail) || same(AutofillField::kPhone) ||
          (name_near && near(AutofillField::kStreet))) {
        rank = 1;
      }
    }
    if (rank > best_rank ||
        (rank == best_rank && rank > 0 && overlap > best_overlap)) {
      best_rank = rank;
      best_overlap = overlap;
      best.entry = index;
      best.new_fields = new_fields;
      best.changed_fields = rank == 1 ? changed : std::vector<AutofillField>{};
      best.kind = rank == 3   ? AutofillComparison::Kind::kSame
                  : rank == 2 ? AutofillComparison::Kind::kAddsInformation
                              : AutofillComparison::Kind::kSimilar;
    }
  }
  if (best_rank == 0) {
    best = AutofillComparison{};
  }
  return best;
}

bool AreSimilarEntries(const AutofillEntry& a,
                       const AutofillEntry& b,
                       std::string_view country) {
  AutofillValues values;
  for (std::size_t i = 0; i < kAutofillStoredFields; ++i) {
    if (!a.values[i].empty()) {
      values[static_cast<AutofillField>(i)] = a.values[i];
    }
  }
  AutofillComparison result = CompareWithEntries(values, {b}, country);
  if (result.kind != AutofillComparison::Kind::kDifferent) {
    return true;
  }
  // Also the other way round: b may add to a.
  values.clear();
  for (std::size_t i = 0; i < kAutofillStoredFields; ++i) {
    if (!b.values[i].empty()) {
      values[static_cast<AutofillField>(i)] = b.values[i];
    }
  }
  return CompareWithEntries(values, {a}, country).kind !=
         AutofillComparison::Kind::kDifferent;
}

std::vector<std::vector<std::size_t>> FindDuplicateEntries(
    const std::vector<AutofillEntry>& entries,
    std::string_view country) {
  std::vector<int> group(entries.size(), -1);
  std::vector<std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    for (std::size_t j = i + 1; j < entries.size(); ++j) {
      if (!AreSimilarEntries(entries[i], entries[j], country)) {
        continue;
      }
      if (group[i] < 0 && group[j] < 0) {
        group[i] = group[j] = static_cast<int>(groups.size());
        groups.push_back({i, j});
      } else if (group[i] >= 0 && group[j] < 0) {
        group[j] = group[i];
        groups[static_cast<std::size_t>(group[i])].push_back(j);
      } else if (group[j] >= 0 && group[i] < 0) {
        group[i] = group[j];
        groups[static_cast<std::size_t>(group[j])].push_back(i);
      }
    }
  }
  for (std::vector<std::size_t>& g : groups) {
    std::ranges::sort(g);
  }
  return groups;
}

AutofillValues ToStoredValues(const AutofillValues& form_values) {
  AutofillValues stored;
  std::string line1;
  std::string line2;
  for (const auto& [field, value] : form_values) {
    if (value.empty()) {
      continue;
    }
    switch (field) {
      case AutofillField::kFullName: {
        std::size_t space = value.rfind(' ');
        if (space == std::string::npos) {
          stored.try_emplace(AutofillField::kFirstName, value);
        } else {
          stored.try_emplace(AutofillField::kFirstName, value.substr(0, space));
          stored.try_emplace(AutofillField::kLastName, value.substr(space + 1));
        }
        break;
      }
      case AutofillField::kStreetLine1:
        line1 = value;
        break;
      case AutofillField::kStreetLine2:
        line2 = value;
        break;
      default:
        stored[field] = value;
    }
  }
  if (!line1.empty() || !line2.empty()) {
    stored[AutofillField::kStreet] = line2.empty()   ? line1
                                     : line1.empty() ? line2
                                                     : line1 + "\n" + line2;
  }
  return stored;
}

bool WorthSaving(const AutofillValues& stored_values) {
  std::size_t count = 0;
  bool key = false;
  for (const auto& [field, value] : stored_values) {
    if (!value.empty()) {
      ++count;
      key = key || IsNameOrContact(field);
    }
  }
  return count >= 3 && key;
}

std::optional<std::string> ValidateAutofillValue(AutofillField field,
                                                 std::string_view value) {
  std::string compact = NormalizeCompact(value);
  if (compact.empty()) {
    return std::nullopt;
  }
  switch (field) {
    case AutofillField::kIco:
      if (!ValidIco(compact)) {
        return "This is not a valid IČO (8 digits with a check digit).";
      }
      break;
    case AutofillField::kDic:
      if (compact.starts_with("CZ")) {
        std::string digits = compact.substr(2);
        if (!IsDigits(digits) || digits.size() < 8 || digits.size() > 10) {
          return "A Czech DIČ is CZ followed by 8 to 10 digits.";
        }
      } else if (!ValidVatId(compact)) {
        return "This is not a valid DIČ (e.g. CZ12345678).";
      }
      break;
    case AutofillField::kVatId:
      if (!ValidVatId(compact)) {
        return "This is not a valid EU VAT ID for its country (e.g. "
               "CZ12345678, DE123456789).";
      }
      break;
    case AutofillField::kIban:
      if (!ValidIban(compact)) {
        return "This IBAN is not valid (its check digits do not match).";
      }
      break;
    default:
      break;
  }
  return std::nullopt;
}

}  // namespace brouk
