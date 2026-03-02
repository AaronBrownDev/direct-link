# *API Documentation*

- [API Endpoints](#api-endpoints)
- [Data Models](#data-models)
- Integration Guide
- Example Flows
- Error Handling Guide

## API Endpoints

This  section documents the endpoints that will be used by the client to `Create Session`, `Join Session`, `Close Session`, `Get My Sessions`

- **Create Session**
    1. **Purpose**: The endpoint creates a session
    2. **Who calls it**: The director is the only user that create a session.
    3. **Request fields**: User_id (string), Max_Cameras (int 32,default 4)
    4. **Response fields**: Room Code (string)
    5. **All possible errors**:
        - User Id required
        - Max Cameras required
        - Not sure if the client needs these
            1. Failed to grant access
            2. Failed to create session

    6. **An example**:
        - Request {
            "user_id": "director-123",
            "max_cameras": 4,
        }
        - Response{
            "room_code":"ROOM-1234"
        }

- **Join Session**
    1. **Purpose**: The endpoint allows directors and other users to join the session
    2. **Who calls it**: Users that have access
    3. **Request fields**: Room Code (string), UserId(string), Role(string)
    4. **Response fields**: Live kit access Token (string), Live kit server url (string)
    5. **All possible errors**:
        - User Id is required
        - Role is required
        - Room Code is required
        - Session not found
        - Session is closed
        - Failed to grant access
        - Show a generic error for these, ask the user to retry
            1. Failed to generate access token
    6. **An example**:
        - Request {
            "room_code": "ROOM-1234"
            "user_id": "director-123",
            "role": "camera",
        }
        - Response {
            "token": " really long string",
            "livekitUrl": "livekit URL"
        }
- **Close Session**
    1. **Purpose**: This endpoint closes sessions
    2. **Who calls it**: Directors who create the session
    3. **Request fields**: Room Code(string), UserId(string)
    4. **Response fields**: A success message (bool)
    5. **All possible errors**:
        - Room code is required
        - User Id is required
        - Only session owner can close
        - Session not found
        - Failed to close session
    6. **An example**:
        - Request {
            "room_code": "ROOM-XXXX",
            "user_id": "director-1"
            }' localhost:50051 directlink.signaling.SignalingService/CloseSession
        - Reply {
            "success": true
        }

- **Get My Sessions**
    1. **Purpose**: The endpoint allows users to get the sessions they have access to.
    2. **Who calls it**: Users that have access
    3. **Request fields**: UserId (string)
    4. **Response fields**: A list of sessions ( each with `session_id` (string), `room_code`(string), `created_at`(string), `max_cameras`(int 32), `status`(string))
    5. **All possible errors**:
        - User Id is required
        - Failed to retrieve sessions
    6. **An example**:
        - Request {
            "user_id": "director-1"
        }
        - Reply {
            "sessions": [
            {
                "sessionId": "4796a4de-909e-41bf-9391-582da619c6ed",
                "roomCode": "ROOM-9281",
                "createdAt": "1772413469",
                "maxCameras": 4,
                "status": "closed"
            }
  ]
}

## *Data Models*

There are two data models that relate to the data needed for sessions. When one is created, or when the data is called by the user

- Session
    1. The model holds the information the CreateSession call will send to the redis and the grpc server.
    2. Definition
        - ID         string    `json:"id"`
        - RoomCode   string    `json:"room_code"`
        - CreatedBy  string    `json:"created_by"`
        - CreatedAt  time.Time `json:"created_at"`
        - MaxCameras int32     `json:"max_cameras"`
        - Status     string    `json:"status"` // active or closed

- Session Info
    1. This model holds the information in the GetMySessions will return.
    2. Definition
        - string session_id = 1; // Internal UUID, informational only
        - string room_code = 2; // Primary identifier
        - string status = 3; // "active" or "closed"
        - int32 max_cameras = 4;
        - string created_at = 5; // RFC3339 formatted timestamp

## *Integration Guide*

This section details the process of setting up a grpc client and connecting to the server.

1. **Prerequisites**
    - protoc (the proto compiler)
    - The gRPC C++ plugin for protoc (grpc_cpp_plugin)
    - The gRPC C++ runtime library
    - Qt 6.5+ for a the native Qt gRPC module, otherwise the standard gRPC C++ library works standalone.

2. **Generating proto file**
    - The file is located at `shared/protocol/signaling.proto`.
    - **Note**: Do not copy this file, this will prevent the client from getting updates when the proto file is updated.
    - This provides the files `signaling.pb.h`,`signaling.pb.cc`, `signaling.grpc.pb.h`, and `signaling.grpc.pb.cc`.

3. **Setting up connection**
    - Here is an example of how to set up the grpcurl connection. **Note**: For local development change `<STAGING_HOST>` to `localhost`.
    >auto channel = grpc::CreateChannel("<STAGING_HOST>:50051", grpc::SslCredentials({}));
    >auto stub = SignalingService::NewStub(channel);

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
    - Receive token and livekit_url
    - Connect to LiveKit using the LiveKit SDK with those values

3. Closing flow:
    - Director calls CloseSession with their user_id and room_code
    - On success, disconnect from LiveKit and return to the home screen

## *Error handling guide*

This section defines how to handle error types.

| grpc Status          | Plain meaning                      | General Meaning                             |
|----------------------|------------------------------------|---------------------------------------------|
| `InvalidArgument`    | You sent a bad or missing field    | Check request fields, show validation error |
| `NotFound`           | Room Code does not exist           | Tell user the room code is invalid          |
| `FailedPrecondition` | Session exists but is closed       | Tell user the session has ended             |
| `Internal`           | Something went wrong on the server | Show generic error, ask user to retry       |
| `PermissionDenied`   | You do not have rights to do this  | Tell user they are not the session owner    |
