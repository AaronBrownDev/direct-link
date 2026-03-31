TODO

1. Latency chip + indicator
- Must retrieve the latency of a stream. Check if it be done through livekit
- Based on the config, update `DirectorTransport`, `DirectorSession`, and/or `VideoTrack` and expose it in `CameraFeed` and `CameraList`
  - SessionPage can reference it to set the chip based on active CameraList
  - `CameraFeed` can update `Thumbnail` border based on what range the value falls in
- Add a default/disabled state to the chip for when a camera is not selected
  - Change color based on what range the value falls in
2. Copy Button
- Add a copy button to certain controls that display room codes to allow the copying of room codes
- Controls include:
  - RecentSessionList
  - SessionInfo
  - Session creation popup in Main.qml
3. AppLog
4. Reconnect Popup
5. Session Timer
6. LIVE indicator