## TODO

### UI Controls

1. Latency chip + indicator
- Must retrieve the latency of a stream.
- See docs/api 
- Add a default/disabled state to the chip for when a camera is not selected
  - Change color based on what range the value falls in
2. Copy Button
- Add a copy button to certain controls that display room codes to allow the copying of room codes
- Controls include:
  - RecentSessionList
  - SessionInfo
  - Session creation popup in Main.qml
3. AppLog
4. Session Timer
5. LIVE indicator

### Packaging

- Move package-linux.sh script to direct-link/scripts directory (edit as needed)
- Delete client/scripts directory
- Add a background to the app icon
- Create documentation for Linux packaging workflow (docs/deployment/linux_packaging_guide.md)
- Set up Windows packaging workflow + documentation (docs/deployment/windows_packaging_guide.md)