#include <iostream>
#include <thread>
#include <chrono>
#include "cdp/browser.h"

int main() {
    cdp::Browser browser("127.0.0.1", 9222);

    if (!browser.connect()) {
        std::cerr << "[-] Failed to connect to Chrome. Is it running with --remote-debugging-port=9222?\n";
        return 1;
    }

    std::cout << "[+] Connected to Chrome successfully.\n\n";

    // Register a callback that fires whenever Chrome navigates to a new URL
    auto urlId = browser.addOnNewURL([](const std::string& url) {
        std::cout << "[URL] " << url << std::endl;
        });

    std::cout << "[*] Watching for URL changes...\n";
    std::cout << "[*] Open new tabs or navigate in Chrome to see live updates.\n\n";

    // Start the background watcher thread (required for URL callbacks + future input monitoring)
    if (!browser.watchInput()) {
        std::cerr << "[-] Failed to start watcher.\n";
        return 1;
    }

    // Run for 2 minutes
    std::this_thread::sleep_for(std::chrono::minutes(2));

    // Cleanup
    browser.stopInput();
    browser.removeOnNewURL(urlId);

    std::cout << "\n[+] Done.\n";
    return 0;
}
