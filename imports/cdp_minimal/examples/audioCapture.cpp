#include <iostream>
#include <thread>
#include <chrono>
#include "cdp/browser.h"

int main() {
    cdp::Browser browser("127.0.0.1", 9222);

    if (!browser.connect()) {
        std::cerr << "[-] Failed to connect to Chrome\n";
        return 1;
    }

    std::cout << "[+] Connected to Chrome\n";

    // Grant microphone permission
    browser.grantPermissions({ "audioCapture" });

    // List microphones
    std::cout << "\n=== Available Microphones ===\n";
    auto mics = browser.listMicrophones();
    if (mics && !mics.value().empty()) {
        for (size_t i = 0; i < mics.value().size(); ++i) {
            std::cout << "[" << i << "] " << mics.value()[i] << "\n";
        }
    }
    else {
        std::cout << "No microphones found or permission denied.\n";
        return 1;
    }

    // === Record and save a 5-second clip ===
    const int durationMs = 5000;
    std::cout << "\n[+] Recording " << (durationMs / 1000)
        << "s of audio to mic_clip.webm...\n";
    auto result = browser.saveAudioClip("mic_clip.webm", 5, durationMs);

    if (result) {
        std::cout << "[+] Successfully saved: mic_clip.webm\n";
    }
    else {
        std::cerr << "[-] Failed: " << result.error().message << "\n";
    }

    std::cout << "\n[+] Done.\n";
    return 0;
}
