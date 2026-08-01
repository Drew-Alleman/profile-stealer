#pragma once

#include <string>

struct TerminationResult {
    bool        ok = false;
    int         errorCode = 0;      // GetLastError() on Windows, errno on Linux
    std::string detail;

    explicit operator bool() const { return ok; }
};


class TerminatorBase {
public:
    explicit TerminatorBase(uint64_t pid) : m_pid(pid) {}

    bool isValid() const {
        return m_pid > 0;
    }

protected:
    int m_pid = 0;
};