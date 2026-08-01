#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <optional>
#include <filesystem>

namespace fs = std::filesystem;

enum class Browser { Chrome, Edge, Brave };

// Candidate install locations for a browser, in priority order, per OS.
std::vector<fs::path> browserCandidates(Browser b);

// Resolve to a single UTF-8 path: the first candidate that exists on disk, or
// the first candidate as a best guess if none are found.
std::optional<std::string> resolveBrowser(Browser b);

// Name string -> Browser  ("Chrome", "chrome", "CHROME" all work).
std::optional<Browser> browserFromName(std::string_view name);

// Convenience: resolve straight from a name string.
std::optional<std::string> resolveBrowser(std::string_view name);

// Name -> UTF-8 path map. Only browsers found (or with a known default
// location) are included.
std::map<std::string, std::string> browserPathMap();