#ifndef BROUK_CEF_BREACH_CHECK_H_
#define BROUK_CEF_BREACH_CHECK_H_

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace brouk {

// Checks passwords against Have I Been Pwned (008) with k-anonymity: only
// the 5-character hash prefix of each password leaves Brouk (see
// core/password_checkup.h), with padded answers so their size tells
// nothing either. `done` gets, for each password, how often it appears in
// breaches (0 when not found or the check failed), on the UI thread. UI
// thread.
using BreachDone = std::function<void(std::vector<std::int64_t> counts)>;
void CheckBreaches(std::vector<std::string> passwords, BreachDone done);

}  // namespace brouk

#endif  // BROUK_CEF_BREACH_CHECK_H_
