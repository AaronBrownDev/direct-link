# *API Documentation*

- [Overview](#overview)
- [API Endpoints](#api-endpoints)
- [Data Models](#data-models)
- [Integration Guide](#integration-guide)
- [Example Flows](#example-flows)
- [Error Handling Guide](#error-handling-guide)
- [HTTP Health Endpoints](#http-health-endpoints)

## *Overview*

The signaling server is the **control plane** for DirectLink. It manages production sessions and issues LiveKit access tokens for directors and WHIP credentials for camera operators, so clients can stream media. The server does not route video or audio — that is handled entirely by LiveKit after the client receives a token.

**What the signaling server does:**

- Creates and tracks production sessions in Redis.
- Maps human-readable room codes (e.g. `ROOM-000472`) to internal session IDs.
- Generates tokens using role-based permissions.
- Enforces ownership rules (only the session creator can close it).
- Exposes Prometheus metrics and Kubernetes health probes.

**What it does not do:**

- Route media (LiveKit handles WebRTC).
- Authenticate users against an identity provider (MVP uses simple user IDs).
- Manage camera hardware or encoding settings.

## *API Endpoints*

This section documents the endpoints that will be used by the client to `Create Session`, `Join Session`, `Close Session`, `Get My Sessions`.

- **Create Session**
    1. **Purpose**: This endpoint creates a session.
    2. **Who calls it**: The director is the only user that can create a session.
    3. **Request fields**:
        - **User_id (string)**: Identifier for the director creating the session. Becomes the session owner.
        - **Max_Cameras (int32)**: Maximum number of camera operators allowed. Must be greater than 0.
    4. **Response fields**:
        - **Room Code (string)**: Human readable code (format: ROOM-XXXX) to share with participants.
    5. **All possible errors**:
        - `Invalid Argument`: User Id required
        - `Invalid Argument`: Max Cameras required
        - `Internal`: Show a generic error for these, ask the user to retry
            1. Failed to grant access
            2. Failed to create session

    6. **An example**:
    ```
    - Request:
        "user_id": "director-123"
        "max_cameras": 4
    - Response:
        "room_code": "ROOM-123456"
    ```

- **Join Session**
    1. **Purpose**: The endpoint allows directors and other users to join the session.
    2. **Who calls it**: Users that have access.
    3. **Request fields**:
        - **Room Code (string)**: The ROOM-XXXXXX code received from the session creator
        - **UserId (string)**: Identifier for the participant joining
        - **Role (string)**: Must be exactly `"camera"` or `"director"`
    4. **Response fields**:
        - Directors
            1. **LiveKit Access Token (string)**: LiveKit JWT access token.
            2. **LiveKit Server URL (string)**: LiveKit server URL for the client to connect to (e.g. `ws://localhost:7880`)
        - Camera Operators
            1. **WHIP URL (string)**: The WHIP URL endpoint (cameras only)
            2. **Stream Key (string)**: The stream key for camera operators.
    5. **All possible errors**:
        - `Invalid Argument`: User Id is required
        - `Invalid Argument`: Role is required
        - `Invalid Argument`: Room Code is required
        - `Not Found`: Session not found
        - `Failed Precondition`: Session is closed
        - `Internal`: Failed to grant access
        - `Internal`: Failed to generate access token
    6. **An example**:
    ```
    - Request:
        "room_code": "ROOM-123456"
        "user_id": "director-123"
        "role": "camera" (or "director")
    - Response:
        1. Director
            "token": "really long string"
            "livekitUrl": "livekit URL"
        2. Camera Operator
            "whip_url": "whip_url"
            "stream_key": "key"
    ```

- **Close Session**
    1. **Purpose**: This endpoint closes sessions
    2. **Who calls it**: Directors who create the session
    3. **Request fields**:
        - **Room Code (string)**: The ROOM-XXXXXX code for the session to close
        - **UserId (string)**: Must match the session owner
    4. **Response fields**:
        - **Success (bool)**: Whether the session was closed successfully
    5. **All possible errors**:
        - `Invalid Argument`: Room code is required
        - `Invalid Argument`: User Id is required
        - `Permission Denied`: Only session owner can close
        - `Not Found`: Session not found
        - `Internal`: Failed to close session
    6. **An example**:
    ```
    - Request:
        "room_code": "ROOM-XXXXXX"
        "user_id": "director-1"
    - Reply:
        "success": true
    ```

- **Get My Sessions**
    1. **Purpose**: The endpoint allows users to get the sessions they have access to.
    2. **Who calls it**: Directors
    3. **Request fields**:
        - **UserId (string)**: The director whose sessions to fetch
    4. **Response fields**: A list of sessions (each with `session_id` (string), `room_code` (string), `created_at` (string), `max_cameras` (int32), `status` (string))
    5. **All possible errors**:
        - `Invalid Argument`: User Id is required
        - `Internal`: Failed to retrieve sessions
    6. **An example**:
    ```
    - Request:
        "user_id": "director-1"
    - Reply:
        "sessions": [
            {
                "sessionId": "4796a4de-909e-41bf-9391-582da619c6ed",
                "roomCode": "ROOM-9281",
                "createdAt": "2026-03-10T15:30:45Z",
                "maxCameras": 4,
                "status": "closed"
            }
        ]
    ```
    **Important Notes for Client Team**:

    - Closing a session does not disconnect LiveKit participants immediately. LiveKit may still have active connections until they time out or the LiveKit room is explicitly destroyed. The signaling server currently marks the session as closed in Redis, which prevents new joins.
    - A closed session cannot be reopened. The director must create a new session.
    - **Calling JoinSession multiple times is safe.** Each call generates fresh credentials (a new JWT token for directors, a new WHIP ingress for cameras). The server does not track "connected" vs "disconnected" state — only whether the session is active or closed.
    - **Camera operators can rejoin after disconnecting** by calling JoinSession again with the same room code. They do not need a new room code from the director.
    - **Director tokens expire after 1 hour.** If the director's LiveKit connection drops due to token expiry, call JoinSession again to get a fresh token.

- **GetServerTime**
    1. **Purpose**: Returns the server's current wall-clock time in nanoseconds. Both camera and director machines call this independently to compute their clock offset relative to the server, putting their timestamps in a shared reference frame so end-to-end latency can be measured across machines with independent clocks.
    2. **Who calls it**: Camera operators and directors — any client that needs to correct its local timestamps.
    3. **Request fields**: None.
    4. **Response fields**:
        - **server_time_ns (int64)**: Current server time as a Unix timestamp in nanoseconds.
    5. **All possible errors**: This endpoint is unauthenticated and has no required fields. No application-level errors are expected; treat any `Internal` response as transient and retry.
    6. **An example**:
    ```
    - Request: (empty)
    - Response:
        "server_time_ns": 1743628800000000000
    ```

    **Clock Offset Formula (required for end-to-end latency measurement)**:

    ```
    t1            = client time immediately before sending the request (nanoseconds)
    t2            = client time immediately after receiving the response (nanoseconds)
    server_time   = server_time_ns from the response

    clock_offset  = server_time - (t1 + t2) / 2

    corrected_timestamp = local_time + clock_offset
    ```

    The `(t1 + t2) / 2` midpoint is the client's best estimate of the moment the server recorded its timestamp, assuming equal one-way delays. Network delay is not subtracted — it is absorbed into the midpoint calculation (same approach used by NTP).

    Clients should call `GetServerTime` on a regular interval (e.g. every 5 seconds) and apply a rolling average over recent samples to smooth out jitter. Do not use a single sample.

    **Accuracy note**: The residual error comes from network asymmetry — if the request and response take different amounts of time, the midpoint estimate is off by half the difference. On our GKE dev cluster this asymmetry is typically sub-millisecond, well within the sub-150ms latency target.

    **End-to-end latency pipeline (camera + director coordination required)**:

    - **Camera side**: embed `corrected_stamp = now() + offset_cam` into each frame as an RTP header extension before it enters the GStreamer WHIP pipeline.
    - **Director side**: extract that RTP header extension timestamp on receipt, then compute `e2e_latency = (now() + offset_dir) - corrected_stamp`.

    The specific RTP header extension ID and format must be agreed on between the camera and director implementations before merging.


## *Data Models*

There are two data models that relate to the data needed for sessions. When one is created, or when the data is called by the user.

**Session**

The model holds the information the CreateSession call will send to Redis and the gRPC server.

| Field | Type | Description |
|-------|------|---|
| `id` | `string` | UUID v4. Internal identifier and LiveKit room name. |
| `room_code` | `string` | `ROOM-XXXX` format. Human-readable, shared with participants. |
| `created_by` | `string` | User ID of the session creator/owner. |
| `created_at` | `string` | RFC 3339 formatted UTC timestamp (e.g. `2026-03-10T15:30:45Z`). |
| `max_cameras` | `int32` | Camera limit set at creation. |
| `status` | `string` | `"active"` or `"closed"`. |


**SessionInfo**

This model represents a single session returned by GetMySessions.

| Field | Type | Description |
|-------|------|-------------|
| `session_id` | `string` | UUID v4. Internal identifier (informational only). |
| `room_code` | `string` | `ROOM-XXXX` format. Primary identifier for the session. |
| `status` | `string` | `"active"` or `"closed"`. |
| `max_cameras` | `int32` | Camera limit set at creation. |
| `created_at` | `string` | RFC 3339 formatted UTC timestamp (e.g. `2026-03-10T15:30:45Z`). |

**Redis Key Schema**

Internal storage details — not exposed directly to clients, but useful for debugging.

| Key Pattern | Type | TTL | Description |
|---|---|---|---|
| `session:{session_id}` | Hash | 24h | Session metadata |
| `session:{session_id}:access` | Hash | 24h | User → role mapping |
| `roomcode:{room_code}` | String | 24h | Room code → session ID lookup |
| `user:{user_id}:sessions` | Set | 24h | Set of session IDs created by user |
| `sessions:active` | Sorted Set | — | Active session IDs (scored by expiry time) |

## *Authentication Model*

**MVP approach:** The signaling server uses **simple string user IDs** — there is no password, OAuth, or token-based authentication for the signaling RPC itself. User identity is self-asserted via the `user_id` field.

**Authorization is role-based** and enforced at the LiveKit token level:

| Role | `canPublish` | `canSubscribe` | Description |
|------|:---:|:---:|---|
| `camera` | Yes | No | Sends video/audio to the room. Cannot view other streams. |
| `director` | No | Yes | Receives all streams in the room. Cannot publish. |

These permissions are baked into the LiveKit JWT token, for the director, and WHIP URL, for the camera, returned by `JoinSession`. The LiveKit server enforces them — the signaling server only generates the token.

**Room code as access control:** Knowing a valid, active room code is sufficient to join a session. The signaling server auto-grants access on `JoinSession`.

---

## *Integration Guide*

[grpcurl](https://github.com/fullstorydev/grpcurl) is pre-installed in the dev container. The signaling server has gRPC reflection enabled, so you don't need to point grpcurl at the proto file.

### List Available Services

```bash
grpcurl -plaintext dev:50051 list
```

Expected output:

```
directlink.signaling.SignalingService
grpc.reflection.v1.ServerReflection
grpc.reflection.v1alpha.ServerReflection
```

### Describe the Service

```bash
grpcurl -plaintext dev:50051 describe directlink.signaling.SignalingService
```

### CreateSession

```bash
grpcurl -plaintext \
  -d '{"user_id": "director-test", "max_cameras": 4}' \
  dev:50051 \
  directlink.signaling.SignalingService/CreateSession
```

Expected response:

```json
{
  "roomCode": "ROOM-000472"
}
```

### JoinSession (Director)

```bash
grpcurl -plaintext \
  -d '{"room_code": "ROOM-000472", "user_id": "director-test", "role": "director"}' \
  dev:50051 \
  directlink.signaling.SignalingService/JoinSession
```

Expected response:

```json
{
  "token": "eyJhbGciOiJIUzI1...",
  "livekitUrl": "ws://localhost:7880"
}
```

### JoinSession (Camera)

```bash
grpcurl -plaintext \
  -d '{"room_code": "ROOM-000472", "user_id": "camera-test", "role": "camera"}' \
  dev:50051 \
  directlink.signaling.SignalingService/JoinSession
```

Expected response:

```json
{
  "whipUrl": "http://localhost:8080/whip",
  "streamKey": "9JNMrfddGxQN"
}
```

### CloseSession

```bash
grpcurl -plaintext \
  -d '{"room_code": "ROOM-000472", "user_id": "director-test"}' \
  dev:50051 \
  directlink.signaling.SignalingService/CloseSession
```

Expected response:

```json
{
  "success": true
}
```

### GetMySessions

```bash
grpcurl -plaintext \
  -d '{"user_id": "director-test"}' \
  dev:50051 \
  directlink.signaling.SignalingService/GetMySessions
```

Expected response:

```json
{
  "sessions": [
    {
      "sessionId": "a1b2c3d4-e5f6-7890-abcd-ef1234567890",
      "roomCode": "ROOM-000472",
      "createdAt": "2026-03-10T15:30:45Z",
      "maxCameras": 4,
      "status": "closed"
    }
  ]
}
```

### GetServerTime

Record `t1` immediately before the call and `t2` immediately after. The clock offset is `server_time_ns - (t1 + t2) / 2`.

```bash
grpcurl -plaintext \
  dev:50051 \
  directlink.signaling.SignalingService/GetServerTime
```

Expected response:

```json
{
  "serverTimeNs": 1743628800000000000
}
```

## *Example Flows*

Three flows, written as numbered pseudocode steps:

1. Director Flow
    - Call CreateSession with their user ID and camera limit
    - Receive room_code — display this to the user so they can share it
    - Call JoinSession with role: "director" and the same room_code
    - Receive token and livekit_url
    - Connect to LiveKit using the LiveKit SDK with those values

2. Camera Flow
    - User enters the room_code they received (e.g. typed in a dialog)
    - Call JoinSession with role: "camera" and that room_code
    - Receive stream key and whip_url
    - Connect to LiveKit Ingress Service via WHIP with these values

3. Closing Flow
    - Director calls CloseSession with their user_id and room_code
    - On success, disconnect from LiveKit and return to the home screen. For camera operators, stops the WHIP stream

## *Error Handling Guide*

This section defines how to handle error types.

| gRPC Status          | Plain meaning                      | General Meaning                             |
|----------------------|------------------------------------|---------------------------------------------|
| `InvalidArgument`    | You sent a bad or missing field    | Check request fields, show validation error |
| `NotFound`           | Room Code does not exist           | Tell user the room code is invalid          |
| `FailedPrecondition` | Session exists but is closed       | Tell user the session has ended             |
| `Internal`           | Something went wrong on the server | Show generic error, ask user to retry       |
| `PermissionDenied`   | You do not have rights to do this  | Tell user they are not the session owner    |

## HTTP Health Endpoints

These are served on port `8081` and are intended for Kubernetes probes and manual debugging.

| Endpoint | Method | Purpose | Healthy Response |
|----------|--------|---------|-----------------|
| `/healthz` | GET | General health check | `200` `{"status": "available"}` |
| `/readyz` | GET | Readiness probe (checks Redis) | `200` `{"status": "ready"}` |
| `/livez` | GET | Liveness probe | `200` `{"status": "alive"}` |
| `/metrics` | GET | Prometheus metrics scrape endpoint | Prometheus text format |

### Readiness Failure

If Redis is unreachable, `/readyz` returns `503`:

```json
{
  "status": "not_ready",
  "service": "signaling",
  "reason": "redis_unavailable"
}
```

[Back to the top](#overview)
