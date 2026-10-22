#ifndef BROUK_CORE_RESULT_H_
#define BROUK_CORE_RESULT_H_

#include <string>
#include <utility>
#include <variant>

namespace brouk {

// An error for the user or the log: what went wrong, in plain words.
struct Error {
  std::string message;
};

// A value or an error. We do not throw (CLAUDE.md, "Errors"); functions that
// can fail return this instead. (std::expected is C++23.)
template <typename T>
class Result {
 public:
  Result(T value) : data_(std::move(value)) {}      // NOLINT: implicit.
  Result(Error error) : data_(std::move(error)) {}  // NOLINT: implicit.

  bool ok() const { return std::holds_alternative<T>(data_); }
  explicit operator bool() const { return ok(); }

  // Only when ok().
  const T& value() const& { return std::get<T>(data_); }
  T& value() & { return std::get<T>(data_); }
  T&& value() && { return std::get<T>(std::move(data_)); }
  // Only when !ok().
  const Error& error() const { return std::get<Error>(data_); }

 private:
  std::variant<T, Error> data_;
};

}  // namespace brouk

#endif  // BROUK_CORE_RESULT_H_
