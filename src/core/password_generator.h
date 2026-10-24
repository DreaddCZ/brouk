#ifndef BROUK_CORE_PASSWORD_GENERATOR_H_
#define BROUK_CORE_PASSWORD_GENERATOR_H_

#include <string>

namespace brouk {

// What generated passwords (008) contain; the setting passwords.generator.
struct PasswordRules {
  int length = 20;  // 8 to 64.
  bool upper = true;
  bool lower = true;
  bool digits = true;
  bool symbols = true;
};

// A random password with at least one character of every enabled kind.
// Like Chrome, it leaves out characters that are easy to confuse (l, I, O,
// 0, 1) and uses only symbols that sites rarely reject (- _ . : !). If no
// kind is enabled, lower case letters and digits are used.
std::string GeneratePassword(const PasswordRules& rules);

}  // namespace brouk

#endif  // BROUK_CORE_PASSWORD_GENERATOR_H_
