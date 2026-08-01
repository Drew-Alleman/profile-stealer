#pragma once
#include "console.h"
#include <filesystem>

class Command;
struct Context {
    Output                out;
    std::filesystem::path outputDir = ".";
    Command* selected = nullptr;
    std::string profileDirectory;
    bool kill = false;
};

