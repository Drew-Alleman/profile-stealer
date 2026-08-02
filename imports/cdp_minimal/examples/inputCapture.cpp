#include "cdp/browser.h"
#include <iostream>
#include <cdp/input_event.h>

int main() {
    cdp::Browser browser("127.0.0.1", 9222);

    if (!browser.connect()) {
        std::cerr << "Failed to connect to browser." << std::endl;
        return 1;
    }

    // 1. Define the callback
    auto inputHandler = [](const cdp::InputEvent& ev) {
        std::visit([](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, cdp::MouseEvent>) {
                std::cout << "Mouse at: " << arg.x << ", " << arg.y << std::endl;
            }
            else if constexpr (std::is_same_v<T, cdp::KeyboardEvent>) {
                std::cout << "Key pressed: " << arg.key << std::endl;
            }
            }, ev);
        };


    // 2. Register the callback and store the ID
    auto callbackId = browser.addOnInput(inputHandler);

    // 3. Start watching for input
    // The watchInput method triggers the background thread and injects JS
    browser.watchInput();

    // Keep the application running to receive events
    std::cout << "Monitoring input... Press Enter to stop." << std::endl;
    std::cin.get();

    // 4. Cleanup
    browser.removeOnInput(callbackId);
    browser.stopInput();

    return 0;
}
