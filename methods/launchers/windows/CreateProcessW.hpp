#pragma once

#include <windows.h>
#include "launchers/launcher_common.h"
#pragma comment(lib, "kernel32.lib")

// ===========================================================================
// Win32 Launcher using CreateProcessW
// ===========================================================================
class Launcher final : public LauncherBase {
public:
    using LauncherBase::LauncherBase;   // reuse base ctor: Launcher(path, args)

    const char* backendName() const noexcept { return "win32/CreateProcessW"; }

    LaunchResult launch() const {
        LaunchResult r;

        std::wstring appName = widen(m_path);          // m_path: protected in base
        std::wstring cmd = widen(commandLine());   // commandLine(): from base
        std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
        mutableCmd.push_back(L'\0');

        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        BOOL ok = CreateProcessW(
            appName.c_str(),         // application name
            mutableCmd.data(),       // command line
            nullptr, nullptr,        // process & thread security
            FALSE,                   // inherit handles
            0,                       // creation flags
            nullptr,                 // environment
            nullptr,                 // current directory
            &si,
            &pi);

        if (!ok) {
            r.ok = false;
            r.errorCode = static_cast<int64_t>(GetLastError());
            r.detail = "CreateProcessW failed";
            return r;
        }

        r.ok = true;
        r.pid = pi.dwProcessId;
        r.tid = pi.dwThreadId;
        r.errorCode = 0;
        r.detail = "launched";

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return r;
    }

private:
    // UTF-8 -> UTF-16 (only used at the CreateProcessW boundary)
    static std::wstring widen(const std::string& s) {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
        return w;
    }
};