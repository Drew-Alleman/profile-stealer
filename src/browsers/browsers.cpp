#include "browsers.h"

#include <cstdlib>
#include <cctype>
#include <utility>

#include "stealth/encryption.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

    std::string toUtf8(const fs::path& p) {
#ifdef _WIN32
        const std::wstring& w = p.native();
        if (w.empty()) return {};
        int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
            static_cast<int>(w.size()),
            nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string out(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
            out.data(), size, nullptr, nullptr);
        return out;
#else
        return p.native();
#endif
    }

} // namespace

// ---------------------------------------------------------------------------
// Candidate install locations
// ---------------------------------------------------------------------------
std::vector<fs::path> browserCandidates(Browser b) {
    std::vector<fs::path> v;

#if defined(_WIN32)
    auto env = [](const wchar_t* key) -> fs::path {
        DWORD needed = GetEnvironmentVariableW(key, nullptr, 0);
        if (needed == 0) return {};
        std::wstring buf(needed, L'\0');
        DWORD written = GetEnvironmentVariableW(key, buf.data(), needed);
        if (written == 0 || written >= needed) return {};
        buf.resize(written);
        return fs::path(buf);
        };

    const fs::path pf = env(CRYPTW(L"ProgramFiles").c_str());
    const fs::path pf86 = env(CRYPTW(L"ProgramFiles(x86)").c_str());
    const fs::path lad = env(CRYPTW(L"LOCALAPPDATA").c_str());

    switch (b) {
    case Browser::Chrome:
        if (!pf.empty())   v.push_back(pf / CRYPTW(L"Google\\Chrome\\Application\\chrome.exe"));
        if (!pf86.empty()) v.push_back(pf86 / CRYPTW(L"Google\\Chrome\\Application\\chrome.exe"));
        if (!lad.empty())  v.push_back(lad / CRYPTW(L"Google\\Chrome\\Application\\chrome.exe"));
        break;
    case Browser::Edge:
        if (!pf86.empty()) v.push_back(pf86 / CRYPTW(L"Microsoft\\Edge\\Application\\msedge.exe"));
        if (!pf.empty())   v.push_back(pf / CRYPTW(L"Microsoft\\Edge\\Application\\msedge.exe"));
        break;
    case Browser::Brave:
        if (!pf.empty())   v.push_back(pf / CRYPTW(L"BraveSoftware\\Brave-Browser\\Application\\brave.exe"));
        if (!pf86.empty()) v.push_back(pf86 / CRYPTW(L"BraveSoftware\\Brave-Browser\\Application\\brave.exe"));
        if (!lad.empty())  v.push_back(lad / CRYPTW(L"BraveSoftware\\Brave-Browser\\Application\\brave.exe"));
        break;
    }

#elif defined(__APPLE__)
    switch (b) {
    case Browser::Chrome:
        v.push_back(CRYPT("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"));
        break;
    case Browser::Edge:
        v.push_back(CRYPT("/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"));
        break;
    case Browser::Brave:
        v.push_back(CRYPT("/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"));
        break;
    }

#elif defined(__linux__)
    switch (b) {
    case Browser::Chrome:
        v.push_back(CRYPT("/usr/bin/google-chrome"));
        v.push_back(CRYPT("/usr/bin/google-chrome-stable"));
        v.push_back(CRYPT("/opt/google/chrome/chrome"));
        break;
    case Browser::Edge:
        v.push_back(CRYPT("/usr/bin/microsoft-edge"));
        v.push_back(CRYPT("/usr/bin/microsoft-edge-stable"));
        v.push_back(CRYPT("/opt/microsoft/msedge/msedge"));
        break;
    case Browser::Brave:
        v.push_back(CRYPT("/usr/bin/brave-browser"));
        v.push_back(CRYPT("/usr/bin/brave"));
        v.push_back(CRYPT("/opt/brave.com/brave/brave-browser"));
        break;
    }
#endif

    return v;
}
// ---------------------------------------------------------------------------
std::optional<std::string> resolveBrowser(Browser b) {
    auto candidates = browserCandidates(b);
    for (const auto& p : candidates) {
        std::error_code ec;
        if (!p.empty() && fs::exists(p, ec))
            return toUtf8(p);
    }
    if (!candidates.empty())
        return toUtf8(candidates.front());
    return std::nullopt;
}

// ---------------------------------------------------------------------------
std::optional<Browser> browserFromName(std::string_view name) {
    std::string k;
    for (char c : name)
        k += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (k == CRYPT("chrome")) return Browser::Chrome;
    if (k == CRYPT("edge"))   return Browser::Edge;
    if (k == CRYPT("brave"))  return Browser::Brave;
    return std::nullopt;
}

std::optional<std::string> resolveBrowser(std::string_view name) {
    if (auto b = browserFromName(name))
        return resolveBrowser(*b);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
std::map<std::string, std::string> browserPathMap() {
    std::map<std::string, std::string> m;
    const std::pair<const char*, Browser> all[] = {
        {CRYPT("chrome").c_str(), Browser::Chrome},
        {CRYPT("edge").c_str(),   Browser::Edge},
        {CRYPT("brave").c_str(),  Browser::Brave},
    };
    for (const auto& [name, b] : all)
        if (auto p = resolveBrowser(b))
            m.emplace(name, *p);
    return m;
}