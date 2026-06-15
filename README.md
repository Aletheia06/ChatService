# ChatService

Stage 1 is a muduo-based TCP chat skeleton with user login, private messages, and chat rooms. The server and client speak a simple line-delimited JSON protocol: each TCP message is one JSON object followed by `\n`.

## Architecture

- `server/ChatServer.*` integrates muduo `EventLoop` and `TcpServer`, owns connection lifecycle, and routes requests.
- `server/ChatSession.*` stores per-connection state, including the logged-in username.
- `server/UserManager.*` maintains the thread-safe online user map and enforces unique usernames.
- `server/RoomManager.*` maintains thread-safe room membership.
- `client/` contains the muduo `TcpClient` wrapper and an interactive command-line client.
- `common/Json.*` implements the small JSON object parser and serializer used by the wire protocol.
- `common/Protocol.*` translates CLI commands into JSON requests and provides shared protocol helpers.
- `tests/` contains focused CTest checks for configuration and protocol behavior.

The server listens on `127.0.0.1:8888`. Multiple clients can connect at the same time. A client must login before private chat or room operations.

## Directory Structure

```text
ChatService/
|-- CMakeLists.txt
|-- README.md
|-- client/
|   |-- ChatClient.cc
|   |-- ChatClient.h
|   `-- main.cc
|-- common/
|   |-- Config.h
|   |-- Json.cc
|   |-- Json.h
|   |-- Protocol.cc
|   `-- Protocol.h
|-- server/
|   |-- ChatServer.cc
|   |-- ChatServer.h
|   |-- ChatSession.cc
|   |-- ChatSession.h
|   |-- RoomManager.cc
|   |-- RoomManager.h
|   |-- UserManager.cc
|   |-- UserManager.h
|   `-- main.cc
|-- tests/
|   |-- CMakeLists.txt
|   |-- config_test.cc
|   `-- protocol_test.cc
`-- muduo/
```

## JSON Protocol

Requests and responses are JSON objects with string fields only.

Login:

```json
{"type":"login","username":"alice"}
```

Logout:

```json
{"type":"logout"}
```

List online users:

```json
{"type":"users"}
```

Private message:

```json
{"type":"private","target":"alice","message":"hello"}
```

Create and use rooms:

```json
{"type":"create_room","room":"lobby"}
{"type":"join","room":"lobby"}
{"type":"leave","room":"lobby"}
{"type":"room_msg","room":"lobby","message":"hello everyone"}
```

Server responses use JSON too:

```json
{"type":"ok","message":"logged in as alice"}
{"type":"error","message":"target user is not online"}
{"type":"private","from":"bob","message":"hello"}
{"type":"room","room":"lobby","from":"alice","message":"hello everyone"}
```

## Client Commands

The command-line client accepts command text and converts it to JSON:

```text
LOGIN alice
LOGOUT
USERS
PRIVATE target_username message
CREATE_ROOM room_name
JOIN room_name
LEAVE room_name
ROOM_MSG room_name message
```

You can also paste a raw JSON request that follows the protocol.

## Build

This muduo tree is Linux-oriented, so build this project on Linux or WSL.

```sh
cmake -S . -B build-stage1 -DCMAKE_BUILD_TYPE=Debug
cmake --build build-stage1
ctest --test-dir build-stage1 --output-on-failure
```

## Run

Start the server in one terminal:

```sh
./build-stage1/bin/chat_server
```

Start one or more clients in other terminals:

```sh
./build-stage1/bin/chat_client
```

Example session:

```text
LOGIN alice
CREATE_ROOM lobby
JOIN lobby
ROOM_MSG lobby hello room
PRIVATE bob hello
LOGOUT
```

Type `/quit` or press Ctrl-D to exit the client. Disconnecting also logs the user out and removes that user from all rooms.
