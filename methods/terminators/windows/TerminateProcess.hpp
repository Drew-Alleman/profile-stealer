#pragma once

#include <windows.h>
#include "terminators/terminator_common.h"

#pragma comment(lib, "kernel32.lib")

class Terminator final : public TerminatorBase {
public:
    using TerminatorBase::TerminatorBase;   // reuse base ctor: Terminator(pid)

    const char* backendName() const noexcept { return "win32/TerminateProcess"; }

    TerminationResult kill() const {
        TerminationResult r;

        if (!isValid()) {                    // inherited from base
            r.detail = "invalid pid";
            return r;                        // r.ok stays false
        }

        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(m_pid));
        if (!h) {
            r.errorCode = static_cast<int>(GetLastError());
            r.detail = "OpenProcess failed";
            return r;
        }

        if (!TerminateProcess(h, 1)) {
            r.errorCode = static_cast<int>(GetLastError());
            r.detail = "TerminateProcess failed";
            CloseHandle(h);
            return r;
        }

        CloseHandle(h);
        r.ok = true;
        r.detail = "terminated";
        return r;
    }
};