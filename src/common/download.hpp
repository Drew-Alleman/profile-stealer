#pragma once
#include <string>
#include <cstdint>

struct DownloadRequest {
    std::string input;      // source file path / URI to read
    std::string destFile;   // where to write it locally
};

struct DownloadResult {
    bool        ok = false;
    int         errorCode = 0;
    std::string detail;
    std::string filePath;
    std::size_t bytes = 0;
    explicit operator bool() const { return ok; }
};
class Downloader {
public:
    virtual ~Downloader() = default;
    virtual const char* name()  const = 0;
    virtual bool           setup() { return true; }
    virtual bool           teardown() { return true; }
    virtual DownloadResult fetch(const DownloadRequest& req) = 0;
};