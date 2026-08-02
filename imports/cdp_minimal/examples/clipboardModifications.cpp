#include <iostream>
#include <thread>
#include <chrono>
#include "cdp/browser.h"

int main() {
    cdp::Browser browser("127.0.0.1", 9224);

    if (!browser.connect()) {
        std::cerr << "[-] Failed to connect to Chrome.\n";
        return 1;
    }

    // Focus the window! (this is required)
    browser.bringToFront();

    std::string newContent = "Injected Content! (this is global to the operating system)";

    auto injectionResult = browser.getClipboardText();

    if (!injectionResult) {
        std::cerr << "[-] Clipboard write failed: " << injectionResult.error().message << "\n";
    }

    std::cout << "[+] Injected text into Clipboard" << std::endl;

    auto result = browser.getClipboardText();

    if (!result) {
        std::cerr << "[-] Clipboard read failed: " << result.error().message << "\n";
        return 1;
    }

    std::cout << "[+] Clipboard content: " << result.value() << std::endl;

    return 0;
}