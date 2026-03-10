# *API Documentation*

- [Overview](#overview)
- [API Endpoints](#api-endpoints)
- [Data Models](#data-models)
- [Integration Guide](#integration-guide)
- [Example Flows](#example-flows)
- [Error Handling Guide](#error-handling-guide)
- [HTTP Health EndPoints](#http-health-endpoints)

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

This  section documents the endpoints that will be used by the client to `Create Session`, `Join Session`, `Close Session`, `Get My Sessions`

- **Create Session**
    1. **Purpose**: This endpoint creates a session
    2. **Who calls it**: The director is the only user that create a session.
    3. **Request fields**: 
        - **User_id (string)**: Identifier for the director creating the session. Becomes the session owner.
        - **Max_Cameras (int 32)**: Maximum number of camera operators allowed. Must be greater than 0. 
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
    - Response
        "room_code":"ROOM-123456"
    ```
        

- **Join Session**
    1. **Purpose**: The endpoint allows directors and other users to join the session
    2. **Who calls it**: Users that have access
    3. **Request fields**: 
        - **Room Code (string)**: The ROOM-XXXXXX code received from the session creator
        - **UserId(string)**: Identifier for the participating joining
        - **Role(string)**: Must be exactly `"camera"` or `"director"`
    4. **Response fields**: 
        - Directors
            1.  **Live kit access Token (string)**: LiveKit JWT access token. 
            2. **Live kit server url (string)**: LiveKit server URL for the client to connect to (e.g `https://localhost:7880`)
        - Camera Operators 
            1.  **Whip URL (string)**: The whip url endpoint (cameras only)
            2.  **Stream Key (string)**: The stream key for camera operators.
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
            "whip_url:"whip_url"
            "stream_key": key        
    ```
- **Close Session**
    1. **Purpose**: This endpoint closes sessions
    2. **Who calls it**: Directors who create the session
    3. **Request fields**: Room Code(string), UserId(string)
    4. **Response fields**: A success message (bool)
    5. **All possible errors**:
        - `Invalid Argument`: Room code is required
        - `Invalid Argument`: User Id is required
        - `Permission Denied`: Only session owner can close
        - `Not Found`: Session not found
        - `Internal`: Failed to close session
    6. **An example**:
    ```
    - Request :
        "room_code": "ROOM-XXXXXX"
        "user_id": "director-1"

        - Reply :
        "success": true
    ```

- **Get My Sessions**
    1. **Purpose**: The endpoint allows users to get the sessions they have access to.
    2. **Who calls it**: Directors
    3. **Request fields**: UserId (string)
    4. **Response fields**: A list of sessions ( each with `session_id` (string), `room_code`(string), `created_at`(string), `max_cameras`(int 32), `status`(string))
    5. **All possible errors**:
        - User Id is required
        - Failed to retrieve sessions
    6. **An example**:
    ```
    - Request :
        "user_id": "director-1"
    
    - Reply :
        "sessions": [
        {
            "sessionId": "4796a4de-909e-41bf-9391-582da619c6ed",
            "roomCode": "ROOM-9281",
            "createdAt": "1772413469",
            "maxCameras": 4,
            "status": "closed"
        }
    ]      
    ```
    **Important Notes for Client Team**: 

    - Closing a session does not disconnect LiveKit participants immediately. LiveKit may still have active connections until they time out or the LiveKit room is explicitly destroyed. The signaling server currently marks the session as closed in Redis, which prevents new joins.
    - A closed session cannot be reopened. The director must create a new session.


## *Data Models*

There are two data models that relate to the data needed for sessions. When one is created, or when the data is called by the user

**Session**

The model holds the information the CreateSession call will send to the redis and the grpc server.

| Field | Type | Description |
|-------|------|---|
| `id` | `string` | UUID v4. Internal identifier and LiveKit room name. |
| `room_code` | `string` | `ROOM-XXXX` format. Human-readable, shared with participants. |
| `created_by` | `string` | User ID of the session creator/owner. |
| `created_at` | `time.Time` | UTC timestamp of creation. |
| `max_cameras` | `int32` | Camera limit set at creation. |
| `status` | `string` | `"active"` or `"closed"`. |


**Session Info**

This model holds the information in the GetMySessions will return.
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

These permissions are baked into the LiveKit JWT token, for the director, and WHIP url, for the camera, returned by `JoinSession`. The LiveKit server enforces them — the signaling server only generates the token.

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

### JoinSession

```bash
grpcurl -plaintext \
  -d '{"room_code": "ROOM-000472", "user_id": "camera-test", "role": "camera/director"}' \
  dev:50051 \
  directlink.signaling.SignalingService/JoinSession
```

Expected response:
- Director
```json
    {
  "token": "eyJhbGciOiJIUzI1...",
  "livekitUrl": "ws://localhost:7880"
}
```
- Camera
```json
{
  "whipUrl": "http://localhost:8080/whip",
  "streamKey": "9JNMrfddGxQN"
}
```

### CloseSession

```bash
grpcurl -plaintext \
  -d '{"room_code": "ROOM-0472", "user_id": "director-test"}' \
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
      "createdAt": "1740825600",
      "maxCameras": 4,
      "status": "closed"
    }
  ]
}
```

## *Example Flows*

Two flows, written as numbered pseudocode steps:

1. Director Flow
    - Call CreateSession with their user ID and camera limit
    - Receive room_code — display this to the user so they can share it
    - Call JoinSession with role: "director" and the same room_code
    - Receive token and livekit_url
    - Connect to LiveKit using the LiveKit SDK with those values

2. Camera flow
    - User enters the room_code they received (e.g. typed in a dialog)
    - Call JoinSession with role: "camera" and that room_code
    - Receive stream key and whip_url
    - Connect to LiveKit Ingress Service via WHIP with these values 

3. Closing flow:
    - Director calls CloseSession with their user_id and room_code
    - On success, disconnect from LiveKit and return to the home screen. For camera operators, stops the WHIP stream 

## *Error handling guide*

This section defines how to handle error types.

| grpc Status          | Plain meaning                      | General Meaning                             |
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
