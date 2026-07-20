# profile-stealer
Chromium browsers leave database files in the users AppData folder these cointain information such as History, Login Data (encrypted), and auto-fill entries. These files are heavily monitored by AV and EDR products. This repository implements two different but simular methods to automate the stealing of these files without tripping any detections on windows linux and MacOS.

# Customizing Execution Workflow
You can use the `build.py` python script to generate the output executable this allows you to customize the following features:
- Bypass methods
- Execution spawning method
- Termination methods
- Sleep Level / frequency

## Bypass Methods
### CDP
we can utilize the `--remote-debugging-port` flag to enable chromium browsers to be controlled remotely then utilize the feature to open local files using the `file://` indicator.

### Headless
We can launch multiple headless Chromium browsers pointing to the local database files. Because these files are in a non-displayable format, they will be downloaded to the user's local Downloads directory.


## Execution Spawning Methods
### Windows
#### ShellExecuteEx
#### CreateProcessW

### Linux
#### X
#### Y

## Termination Methods
### Windows
#### TerminateProcess
### Linux
#### X
