# profile-stealer

> **Legal Disclaimer**  
> This project is provided **strictly for educational and research purposes only**.  
> It is intended to help security researchers, red team operators, and defensive teams understand evasion techniques and improve detection capabilities.  
>  
> You may not use this software to access, extract data from, or interfere with any system you do not own or do not have explicit written permission to test.  
> Unauthorized use of this tool is illegal and may result in criminal and civil liability. The author does not condone or support any malicious or illegal activity.  
>  
> This software is provided "as is", without warranty of any kind. The author accepts no responsibility or liability for any damages, legal consequences, or misuse resulting from the use of this project.  
> By downloading, using, or modifying this code, you agree that you are solely responsible for ensuring your actions comply with all applicable laws and regulations.

Chromium-based browsers store unencrypted user data (history, autofill, etc.) in local SQLite databases. Directly accessing these files from an untrusted process often triggers AV/EDR alerts. profile-stealer routes the download through a legitimate Chromium process so the activity appears expected.

The build.py script acts as a compile-time modular injector. It auto-detects the OS and swaps in the selected backends before compilation, allowing you to customize:
- Bypass methods (CDP, windowspos, ozone)
- Process launchers
- Process terminators
- Sleep timing & jitter, and implementation

---

## Blog Series

This tool is part of a malware development series:

1. Building an EDR-Evasive Chromium Profile Stealer – Architecture & Modular Backends for EDR Evasion
2. Implementing a Build-Time Backend Selection System
3. Designing a Reusable Architecture for Bypass Methods
4. Windows Off-Screen Technique for Stealing Chromium Profiles
5. Extracting Chromium Profiles with the Chrome DevTools Protocol (CDP)
6. Linux Ozone Technique – Headless Chromium Without Process Explosion
7. Reducing Static Strings on Profile-Stealer to Evade AV and EDR Signatures

---

## Build Requirements

### Common Requirements

- **Python** 3.8 or newer (3.10+ recommended)
- **CMake** (must be available on `PATH`)
- Full project source (`src/`, `methods/`, `CMakeLists.txt`, etc.)

> No additional Python packages are required.

### Windows

- **C++ Compiler**: Visual Studio 2019/2022 (Community is fine) **or** Build Tools for Visual Studio with the **"Desktop development with C++"** workload

**Quick check:**
```powershell
python --version
cmake --version
```

### Linux

- **C++ Compiler**: `g++` or `clang++` (C++17 support)
- **Build Tools**: `build-essential` (Debian/Ubuntu) or equivalent

**Quick install (Debian/Ubuntu):**
```bash
sudo apt update
sudo apt install python3 cmake build-essential
```

**Quick check:**
```bash
python3 --version
cmake --version
g++ --version
```

## Execution Spawning Methods (Launchers)

| Platform | Method | Description |
|----------|--------|-------------|
| **Windows** | `ShellExecuteEx` | Launches the browser through the Windows Shell ([ShellExecuteEx](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexa)) with the "open" verb |
| **Windows** | `CreateProcessW` | Uses [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) to start the browser process |
| **Linux** | `posix_spawn` | Spawns the process with [posix_spawn](https://man7.org/linux/man-pages/man3/posix_spawn.3.html) |

Select the method at build time with `-L`:

```bash
# Windows
python build.py -L CreateProcessW
python build.py -L ShellExecuteEx

# Linux
python3 build.py -L posix_spawn
```

---

## Termination Methods

| Platform | Method | Description |
|----------|--------|-------------|
| **Windows** | `TerminateProcess` | Uses the Windows [TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess) API |
| **Linux** | `sigkill` | Sends `SIGKILL` using the Linux [kill](https://man7.org/linux/man-pages/man2/kill.2.html) function |

Select the method at build time with `-T`:

```bash
# Windows
python build.py -T TerminateProcess

# Linux
python3 build.py -T sigkill
```

---

## Sleep Settings

### Timing & Jitter

Control the delay between downloads with:

- `-M` → base sleep in milliseconds
- `-J` → jitter percentage

**Example:**
```bash
python build.py -J 50 -M 1000
```

This results in a random sleep between **500 ms and 1500 ms** each time.

### Implementations

| Platform | Method | Description |
|----------|--------|-------------|
| **Linux** | `generic_linux` | Uses `nanosleep()` |
| **Linux** | `timer_linux` | Uses `timerfd` |
| **Windows** | `generic_windows` | Uses `std::this_thread::sleep_for` |
| **Windows** | `timer_windows` | Uses `CreateWaitableTimer` |

Select the implementation at build time with `-S`:

```bash
# Windows examples
python build.py -S generic_windows
python build.py -S timer_windows

# Linux examples
python3 build.py -S generic_linux
python3 build.py -S timer_linux
```


---

## Bypass Methods

### CDP

Utilizes the `--remote-debugging-port` flag to enable remote control of Chromium browsers, then uses the `file://` protocol to open local database files.

<img width="1891" height="946" alt="cdp_demo" src="https://github.com/user-attachments/assets/55f36859-fec0-4862-bdde-e9acc242ea7e" />

### windowspos (Windows)

Launches Chromium off-screen using `--window-position=-32000,-32000`. Because the targeted files are non-renderable, Chromium automatically downloads them to the user's Downloads folder.

**Example:**
```bash
profile-stealer.exe windowspos --profile "C:\Users\drew\AppData\Local\Google\Chrome\User\Default" --kill --launch edge
```

<img width="1694" height="930" alt="window_position" src="https://github.com/user-attachments/assets/78649329-152c-49fe-bd07-cbf3aed11aa3" />

### ozone (Linux)

Uses `--ozone-platform=headless` to remove the GUI while still respecting Chromium's process singleton. This allows multiple file requests to be routed through a single browser process.

<img width="1713" height="1011" alt="ozone_demo" src="https://github.com/user-attachments/assets/87965240-85dd-41c4-952d-7865f659ab52" />

---

## Roadmap
- [ ] Make the output zip path configurable
- [ ] Allow custom X,Y coordinates for the `windowspos` bypass
