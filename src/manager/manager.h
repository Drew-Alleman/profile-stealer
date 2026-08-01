// src/manager/manager.hpp          ← permanent, never changes
#pragma once
#include "app/context.h"
#include "../common/download.hpp"
#include <vector>

// manager.h
class Manager {
public:
    explicit Manager(Context& ctx);
    void downloadProfile(Downloader& downloader);

private:
    Context& ctx;

    bool createZip(const std::string& zipPath, const std::vector<std::string>& files);
    void archiveAndCleanup(const std::vector<std::string>& files);
};