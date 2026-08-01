#pragma once

#include <spawn.h>       // posix_spawn
#include <sys/types.h>
#include <sys/wait.h>    // if you ever want to reap
#include <unistd.h>      // environ
#include <cstring>
#include <string>
#include <vector>
#include "launchers/launcher_common.h"

#if defined(__APPLE__)
#include <crt_externs.h>
#define LAUNCHER_ENVIRON (*_NSGetEnviron())
#else
extern char** environ;
#define LAUNCHER_ENVIRON environ
#endif

// ===========================================================================
// POSIX Launcher using posix_spawn
// ===========================================================================
class Launcher final : public LauncherBase {
public:
    using LauncherBase::LauncherBase;   // reuse base ctor: Launcher(path, args)

    const char* backendName() const noexcept { return "posix/posix_spawn"; }

    LaunchResult launch() const {
        LaunchResult r;

        // Build argv: [ program, arg0, arg1, ..., nullptr ]
        // POSIX convention: argv[0] is the program name.
        std::vector<char*> argp;
        argp.reserve(m_args.size() + 2);
        argp.push_back(const_cast<char*>(m_path.c_str()));           // argv[0]
        for (const auto& a : m_args)
            argp.push_back(const_cast<char*>(a.c_str()));            // argv[1..]
        argp.push_back(nullptr);                                     // terminator

        pid_t pid = 0;
        int rc = posix_spawn(
            &pid,
            m_path.c_str(),
            nullptr,
            nullptr,
            argp.data(),
            LAUNCHER_ENVIRON);      

        if (rc != 0) {
            r.ok = false;
            r.errorCode = rc;
            r.detail = "posix_spawn failed";
            return r;
        }

        r.ok = true;
        r.pid = pid;
        r.tid = pid;
        r.errorCode = 0;
        r.detail = "launched";
        return r;
    }
};