#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct LaunchResult {
    bool        ok = false;
    uint64_t    pid = 0;    // DWORD on Win, pid_t on POSIX — both fit
    uint64_t    tid = 0;    // 0 where a thread id isn't meaningful
    int64_t     errorCode = 0;    // GetLastError() on Win, errno on POSIX
    std::string detail;           // UTF-8

    explicit operator bool() const { return ok; }
};


class LauncherBase {
public:
    explicit LauncherBase(std::string path, std::vector<std::string> args = {})
        : m_path(std::move(path))
        , m_args(std::move(args))
    {
    }

    // --- accessors ---------------------------------------------------------
    const std::string& path()      const noexcept { return m_path; }
    const std::vector<std::string>& arguments() const noexcept { return m_args; }

    void setPath(std::string p) { m_path = std::move(p); }
    void addArgument(std::string a) { m_args.push_back(std::move(a)); }
    void clearArguments() { m_args.clear(); }

    std::string commandLine() const {
        std::string cmd = "\"" + m_path + "\"";
        for (const auto& a : m_args) cmd += " " + a;
        return cmd;
    }

    std::string parametersOnly() const {
        std::string p;
        for (const auto& a : m_args) p += (p.empty() ? "" : " ") + a;
        return p;
    }

protected:
    std::string              m_path;
    std::vector<std::string> m_args;
};