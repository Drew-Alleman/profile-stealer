#pragma once
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

inline bool& vtEnabled() { static bool v = false; return v; }

inline void initConsole() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        if (SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            vtEnabled() = true;
    }
#else
    vtEnabled() = true;   // ANSI is a given on POSIX terminals
#endif
}

// True when the given FILE* is attached to an interactive terminal (not a file/pipe).
inline bool isTty(std::FILE* stream) {
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

// True when stdout is a terminal. Kept as a free helper for callers that just want to
// know "are we interactive".
inline bool hasConsole() { return isTty(stdout); }

// Portable wide -> UTF-8. On Windows wchar_t is UTF-16 (uses the Win32 converter); on
// POSIX it is UTF-32 (encoded directly).
inline std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
#ifdef _WIN32
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
#else
    std::string out;
    out.reserve(wide.size());
    for (wchar_t wc : wide) {
        unsigned long cp = static_cast<unsigned long>(wc);
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
#endif
}

// ANSI color codes. initConsole() must have run for these to show on classic conhost.
namespace clr {
    inline constexpr const char* green = "\x1b[32m";
    inline constexpr const char* yellow = "\x1b[33m";
    inline constexpr const char* red = "\x1b[31m";
    inline constexpr const char* cyan = "\x1b[36m";
    inline constexpr const char* reset = "\x1b[0m";
}

struct Output {
    bool        verbose = false;
    std::string logPath;
    bool        consoleHidden = false;

private:
    mutable std::ofstream logFile;

    void writeLog(const std::string& level, const std::string& msg) const {
        if (logPath.empty()) return;
        if (!logFile.is_open())
            logFile.open(logPath, std::ios::app);
        if (logFile.is_open())
            logFile << "[" << level << "] " << msg << "\n";
    }

    // Colorize only when the target stream is a real terminal and the user hasn't opted
    // out via NO_COLOR -- so colors never leak into redirected files or pipes.
    static bool hasEnv(const char* name) {
#ifdef _MSC_VER
        size_t len = 0;
        return getenv_s(&len, nullptr, 0, name) == 0 && len > 0;
#else
        return std::getenv(name) != nullptr;
#endif
    }

    bool useColor(const std::ostream& os) const {
        if (consoleHidden) return false;
        static const bool noColor = hasEnv("NO_COLOR");
        if (noColor) return false;
        return isTty(&os == &std::cerr ? stderr : stdout);
    }
    // Marker + message. Prints the text whether or not we're on a terminal (so redirected
    // output still has content); only the color escapes are gated on being a TTY.
    void writeMarker(std::ostream& os, const char* color,
        const char* mark, const std::string& msg) const {
        if (consoleHidden) return;

        std::string_view body{ msg };
        if (!body.empty() && body.back() == '\n') body.remove_suffix(1);

        if (mark && *mark) {
            if (useColor(os)) os << color << mark << clr::reset << " " << body << "\n";
            else              os << mark << " " << body << "\n";
        }
        else {
            os << body << "\n";
        }
    }

public:
    // [+] green -- normal / success
    void print(const std::string& msg) const {
        writeMarker(std::cout, clr::green, "[+]", msg);
        writeLog("info", msg);
    }

    // [!] yellow -- warning
    void warn(const std::string& msg) const {
        writeMarker(std::cout, clr::yellow, "[!]", msg);
        writeLog("warn", msg);
    }

    // [-] red -- error (stderr)
    void error(const std::string& msg) const {
        writeMarker(std::cerr, clr::red, "[-]", msg);
        writeLog("error", msg);
    }

    // Raw line, no marker -- for table output and pre-formatted text.
    void sPrint(const std::string& msg) const {
        writeMarker(std::cout, clr::reset, "", msg);
        writeLog("info", msg);
    }

    // Console only when --verbose; always logged.
    void verbosePrint(const std::string& msg) const {
        if (verbose && !consoleHidden)
            writeMarker(std::cerr, clr::cyan, "[V]", msg);
        writeLog("verbose", msg);
    }
};
