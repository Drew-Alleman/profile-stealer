# profile-stealer
> Disclaimer
This project is provided strictly for educational and research purposes only.
It is intended to help security researchers, red team operators, and defensive teams understand evasion techniques and improve detection capabilities.
You may not use this software to access, extract data from, or interfere with any system you do not own or do not have explicit written permission to test.
Unauthorized use of this tool is illegal and may result in criminal and civil liability. The author does not condone or support any malicious or illegal activity.
This software is provided "as is", without warranty of any kind. The author accepts no responsibility or liability for any damages, legal consequences, or misuse resulting from the use of this project.
By downloading, using, or modifying this code, you agree that you are solely responsible for ensuring your actions comply with all applicable laws and regulations.

Chromium-based browsers store unencrypted user data—such as browsing history and autofill entries—in local SQLite databases. While directly accessing or copying these files from an untrusted process typically triggers security alerts, Profile Stealer abuses native Chromium features to download database files through the legitimate, trusted browser process, aiding in the bypass of AV and EDR detections.

This repository utilizes the build.py script to act as the automated build manager and compile-time modular injector for the profile-stealer framework. It auto-detects the host operating system and dynamically swaps concrete backend implementations into source header files prior to compilation. This allows for the customization of the following options at build time:
- Extraction/Bypass Methods: Selection of native browser interaction channels (e.g., CDP)
- Process Launchers: Execution spawning methods used to start target browsers
- Process Terminators: Techniques used to close running browser instances
- Sleep Timers & Jitter: Customizable sleep intervals and random timing variation to evade fixed-rate behavioral heuristics

Additionally this tool has been apart of my malware development series on my blog:
  1. Building an EDR-Evasive Chromium Profile Stealer - Architecture & Modular Backends for EDR Evasion
  2. Implementing a Build-Time Backend Selection System
  3. Designing a Reusable Architecture for Bypass Methods
  4. Windows Off-Screen Technique for Stealing Chromium Profiles
  5. Extracting Chromium Profiles with the Chrome DevTools Protocol (CDP)
  6. Linux Ozone Technique - Headless Chromium Without Process Explosion
  7. Reducing Static Strings on Profile-Stealer to Evade AV and EDR Signatures

## Build Requirements
### Linux
### Windows

## Bypass Methods
### CDP
we can utilize the `--remote-debugging-port` flag to enable chromium browsers to be controlled remotely then utilize the feature to open local files using the `file://` indicator.
<img width="1891" height="946" alt="cdp_demo" src="https://github.com/user-attachments/assets/55f36859-fec0-4862-bdde-e9acc242ea7e" />


### windowspos
We can launch multiple headless Chromium browsers pointing to the local database files. Because these files are in a non-displayable format, they will be downloaded to the user's local Downloads directory.

Example Usage: `profile-stealer.exe windowspos --profile "C:\Users\drew\AppData\Local\Google\Chrome\User\Default" --kill --launch edge`


<img width="1694" height="930" alt="window_position" src="https://github.com/user-attachments/assets/78649329-152c-49fe-bd07-cbf3aed11aa3" />

### ozone
<img width="1713" height="1011" alt="ozone_demo" src="https://github.com/user-attachments/assets/87965240-85dd-41c4-952d-7865f659ab52" />

## Execution Spawning Methods
### Windows
#### ShellExecuteEx
Launches the browser through the Windows Shell ([ShellExecuteEx](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexa) with the "open" verb), mimicking the Explorer double-click flow so file associations are honored. 

#### CreateProcessW
Uses [CreateProcessW](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createprocessw) to start the browser process.

### Linux
#### posix_spawn
Spawns the chrome process with [posix_spawn](https://man7.org/linux/man-pages/man3/posix_spawn.3.html).
```c++
 int posix_spawn(pid_t *restrict pid, const char *restrict path,
                 const posix_spawn_file_actions_t *restrict file_actions,
                 const posix_spawnattr_t *restrict attrp,
                 char *const argv[restrict],
                 char *const envp[restrict]);
```
## Termination Methods
### Windows
#### TerminateProcess
Uses [TerminateProcess](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess) to kill the spawned chromium process.

```C++
BOOL TerminateProcess(
  [in] HANDLE hProcess,
  [in] UINT   uExitCode
);
```
### Linux
#### Kill
Sends a SIGTERM using the linux [kill](https://man7.org/linux/man-pages/man2/kill.2.html) function to the spawned chromium process.
```c++
       #include <signal.h>

       int kill(pid_t pid, int sig);
```
