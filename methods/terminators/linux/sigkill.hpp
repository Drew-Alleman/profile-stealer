#pragma once

#include <csignal>       // kill, SIGKILL
#include <cerrno>        // errno
#include "terminators/terminator_common.h"

// ===========================================================================
// POSIX Terminator using kill(pid, SIGKILL)
// ===========================================================================
class Terminator final : public TerminatorBase {
public:
    using TerminatorBase::TerminatorBase;   // reuse base ctor: Terminator(pid)

    const char* backendName() const noexcept { return "posix/kill(SIGKILL)"; }

    TerminationResult kill() const {
        TerminationResult r;

        if (!isValid()) {                    // inherited from base
            r.detail = "invalid pid";
            return r;                        // r.ok stays false
        }

        if (::kill(static_cast<pid_t>(m_pid), SIGKILL) != 0) {
            r.errorCode = errno;             // ESRCH, EPERM, etc.
            r.detail = "kill failed";
            return r;
        }

        r.ok = true;
        r.detail = "terminated";
        return r;
    }
};