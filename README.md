# profile-stealer
Chromium browsers leave database files in the users AppData folder these cointain information such as History, Login Data (encrypted), and auto-fill entries. These files are heavily monitored by AV and EDR products. This repository implements two different simular methods to automate the stealing of these files without tripping any detections on windows linux and MacOS.

## Methods
To avoid triggering the EDR we implement 2 different methods to download the required files. Firstly we can utilize the `--remote-debugging-port` flag to enable chromium browsers to be controlled remotely then utilize the feature to open local files using the `file://` indicator. The other option is sideloading an extension and starting the specified browser with access to our local files.
