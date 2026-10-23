#ifndef BROUK_CEF_DEVTOOLS_PREFERENCES_H_
#define BROUK_CEF_DEVTOOLS_PREFERENCES_H_

#include <string>

#include "include/cef_values.h"

namespace brouk {

// The DevTools frontend's own settings (theme, panel layout, ...), which it
// reads and writes through embedder messages (006). Kept in a JSON file in
// the data folder; the values are strings the frontend chose. UI thread.
class DevToolsPreferences {
 public:
  explicit DevToolsPreferences(std::string path);
  DevToolsPreferences(const DevToolsPreferences&) = delete;
  DevToolsPreferences& operator=(const DevToolsPreferences&) = delete;

  // A copy of all values.
  CefRefPtr<CefDictionaryValue> All();
  // Null if not set.
  CefRefPtr<CefValue> Get(const std::string& name);
  void Set(const std::string& name, const std::string& value);
  void Remove(const std::string& name);
  void Clear();

 private:
  void Load();
  void Save();

  const std::string path_;
  CefRefPtr<CefDictionaryValue> values_;
};

}  // namespace brouk

#endif  // BROUK_CEF_DEVTOOLS_PREFERENCES_H_
