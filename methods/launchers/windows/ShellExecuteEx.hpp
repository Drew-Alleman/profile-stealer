#pragma once

#include <windows.h>
#include <shellapi.h>
#include "launchers/launcher_common.h"   // LauncherBase + LaunchResult
#pragma comment(lib, "shell32.lib")

class Launcher final : public LauncherBase {
public:
    using LauncherBase::LauncherBase;   // reuse base ctor: Launcher(path, args)

    const char* backendName() const noexcept { return "shell/ShellExecuteEx"; }

    LaunchResult launch() const {
        LaunchResult r;

        std::wstring file = widen(m_path);            // m_path: protected in base
        std::wstring params = widen(parametersOnly());  // parametersOnly(): from base

        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"open";
        sei.lpFile = file.c_str();
        sei.lpParameters = params.empty() ? nullptr : params.c_str();
        sei.nShow = SW_SHOWNORMAL;

        if (!ShellExecuteExW(&sei)) {
            r.ok = false;
            r.errorCode = static_cast<int64_t>(GetLastError());
            r.detail = "ShellExecuteExW failed";
            return r;
        }

        r.ok = true;
        r.errorCode = 0;

        if (sei.hProcess) {
            r.pid = GetProcessId(sei.hProcess);
            r.detail = "launched";
            CloseHandle(sei.hProcess);
        }
        else {
            r.pid = 0;
            r.detail = "handed to an existing process; no handle";
        }

        return r;
    }

private:
    // UTF-8 -> UTF-16 (only used at the ShellExecuteExW boundary)
    static std::wstring widen(const std::string& s) {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
        return w;
    }
};