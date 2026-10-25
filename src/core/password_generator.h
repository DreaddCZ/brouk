#ifndef BROUK_CORE_PASSWORD_GENERATOR_H_
#define BROUK_CORE_PASSWORD_GENERATOR_H_

#include <string>
#include <string_view>

namespace brouk {

// What generated passwords (008) contain; the setting passwords.generator.
struct PasswordRules {
  int length = 20;  // 8 to 64.
  bool upper = true;
  bool lower = true;
  bool digits = true;
  bool symbols = true;
};

// The rules from the settings passwords.generated_length and
// passwords.generated_characters ("upper, lower, digits, symbols"; unknown
// words are ignored).
PasswordRules PasswordRulesFromSettings(int length,
                                        std::string_view characters);

// A random password with at least one character of every enabled kind.
// Like Chrome, it leaves out characters that are easy to confuse (l, I, O,
// 0, 1) and uses only symbols that sites rarely reject (- _ . : !). If no
// kind is enabled, lower case letters and digits are used.
std::string GeneratePassword(const PasswordRules& rules);

}  // namespace brouk

#endif  // BROUK_CORE_PASSWORD_GENERATOR_H_
