# profile-stealer
A cross-platform chromium browser profile stealer that implements various methods to avoid AV detections. 

Typically AV/edr files alert when non trusted processes attempt to access the chromium database files such as:
```
C:\Users\drew\AppData\Local\Google\Chrome\User Data\Default\Login Data
```

To avoid triggering the EDR we implement 2 different methods to download the required files. Firstly we can utilize the `--remote-debugging-port` flag to enable chromium browsers to be controlled remotely then utilize the feature to open local files using the `file://` indicator. The other option is sideloading an extension and starting the specified browser with access to our local files.
