#ifndef BROUK_CEF_TAB_ENVIRONMENT_COMMANDS_H_
#define BROUK_CEF_TAB_ENVIRONMENT_COMMANDS_H_

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/tab_environment.h"
#include "include/cef_values.h"

namespace brouk {

// A Chrome DevTools Protocol method and its parameters.
using TabEnvironmentCommand =
    std::pair<std::string, CefRefPtr<CefDictionaryValue>>;

// The commands that give the tab `env` (027), one per kind of override;
// overrides that are off are cleared. An empty environment clears all.
std::vector<TabEnvironmentCommand> TabEnvironmentCommands(
    const TabEnvironment& env);

// "Time zone" for "Emulation.setTimezoneOverride", for error messages.
std::string TabEnvironmentCommandName(std::string_view method);

}  // namespace brouk

#endif  // BROUK_CEF_TAB_ENVIRONMENT_COMMANDS_H_
