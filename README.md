# ChatService

Stage 2 is a muduo-based TCP chat system optimized for higher concurrency. It keeps the Stage 1 user, private-message, and room features, then adds a muduo worker thread pool, multiple `EventLoop` instances, lower-lock routing, asynchronous logging, and periodic performance statistics.

The server and client speak a simple line-delimited JSON protocol: each TCP message is one JSON object followed by `\n`.

## Architecture

- `server/ChatServer.*` integrates muduo `EventLoop` and `TcpServer`, configures the worker event-loop pool, owns connection lifecycle, and routes requests.
- `server/ChatSession.*` stores per-connection state, including the logged-in username.
- `server/ChatStorage.*` owns SQLite persistence, history queries, and JSON history export.
- `server/UserManager.*` maintains the thread-safe online user map and supports one-lock connection snapshots for broadcasts.
- `server/RoomManager.*` maintains thread-safe room membership and returns member-name snapshots.
- `server/ServerMetrics.*` records online users, message rate, average latency, total connections, and dropped connections.
- `client/` contains the muduo `TcpClient` wrapper and an interactive command-line client.
- `qt-client/` contains the Qt 6 Widgets GUI client.
- `common/Json.*` implements the small JSON object parser and serializer used by the wire protocol.
- `common/Protocol.*` translates CLI commands into JSON requests and attaches client send timestamps for latency measurement.
- `tests/` contains focused CTest checks for configuration and protocol behavior.

The server binds to `chatservice::kServerHost:chatservice::kServerPort` from `common/Config.h`. Public deployments can bind the server to `0.0.0.0`; keep that separate from the Qt client's default connection host in `qt-client/src/ClientConfig.h`.

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
|   |-- ChatStorage.cc
|   |-- ChatStorage.h
|   |-- RoomManager.cc
|   |-- RoomManager.h
|   |-- ServerMetrics.cc
|   |-- ServerMetrics.h
|   |-- UserManager.cc
|   |-- UserManager.h
|   `-- main.cc
|-- tests/
|   |-- CMakeLists.txt
|   |-- config_test.cc
|   `-- protocol_test.cc
|-- tools/
|   `-- StressClient.cc
|-- qt-client/
`-- muduo/
```

## JSON Protocol

Requests and responses are line-delimited JSON objects. Existing request fields remain string-compatible; the server also accepts numeric scalar fields for history limits and timestamps.

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

Private messages are stored even if the receiver is offline. If the receiver is online, the server also delivers a live event.

Create and use rooms:

```json
{"type":"create_room","room":"lobby"}
{"type":"join","room":"lobby"}
{"type":"leave","room":"lobby"}
{"type":"room_msg","room":"lobby","message":"hello everyone"}
```

Load private or room history:

```json
{"type":"history_private","peer":"alice","limit":50}
{"type":"history_room","room":"lobby","limit":50}
```

The command-line client adds a `sent_at_us` field to generated JSON requests. The server uses it for end-to-end latency. Raw JSON clients may also include that field.

Server responses use JSON too:

```json
{"type":"ok","message":"logged in as alice"}
{"type":"ok","message":"private message saved","target":"bob","id":"12","created_at":"1710000000"}
{"type":"private","from":"bob","message":"hello","id":"13","created_at":"1710000001"}
{"type":"room","room":"lobby","from":"alice","message":"hello everyone","id":"14","created_at":"1710000002"}
{"type":"history_private_result","peer":"alice","messages":[{"id":1,"sender":"alice","receiver":"bob","content":"hello","created_at":1710000000}]}
{"type":"history_room_result","room":"lobby","messages":[{"id":2,"sender":"alice","room":"lobby","content":"hello everyone","created_at":1710000000}]}
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
HISTORY_PRIVATE peer [limit]
HISTORY_ROOM room [limit]
```

You can also paste a raw JSON request that follows the protocol.

## High-Concurrency Design

The server calls `TcpServer::setThreadNum(kWorkerThreadCount)` with `kWorkerThreadCount = 4`. muduo accepts connections on the base loop and distributes established connections across worker `EventLoop` threads, so socket reads and writes do not all run on one loop.

The main shared state is intentionally narrow:

- User lookup is protected by one mutex inside `UserManager`.
- Room membership is protected by one mutex inside `RoomManager`.
- Per-connection `ChatSession` lookup uses muduo connection context instead of a global session map on the message hot path.
- Broadcasts first take short snapshots, then release locks before sending.
- Room broadcast serializes the JSON event once and reuses the same string for every recipient.

This keeps lock hold time bounded by map/set lookup and snapshot creation. Network sends happen after locks are released, which avoids blocking user and room state behind slow clients.

## Metrics And Benchmark Output

The server records:

- `online_users`
- `messages_per_second`
- `average_latency_us`
- `total_messages`
- `total_connections`
- `dropped_connections`

Every `kMetricsIntervalSeconds` seconds, the server prints and logs a benchmark line:

```text
stats online_users=2 messages_per_second=128.4 average_latency_us=731.2 total_messages=642 total_connections=10 dropped_connections=8
```

`messages_per_second` and `average_latency_us` are interval-based. `total_messages`, `total_connections`, and `dropped_connections` are cumulative. If a request has `sent_at_us`, latency is measured from client send time to server handling time. Otherwise, the server falls back to muduo receive time.

## Asynchronous Logging

`server/main.cc` installs muduo `AsyncLogging` through `muduo::Logger::setOutput`. Server logs are written by a background logging thread into files named like:

```text
chatservice.YYYYMMDD-HHMMSS.hostname.pid.log
```

This keeps request-handling event loops from doing synchronous log file writes on the hot path.

## Bottleneck Analysis

The Stage 1 server could bottleneck in four places:

- A single I/O loop would serialize all client socket activity.
- A global session map lookup on every message would add avoidable locking.
- Room broadcast could repeatedly lock user state for each member.
- Synchronous logging could turn disk I/O into request latency.

Stage 2 addresses those points with muduo worker loops, connection-context session lookup, one-lock recipient snapshots, reusable serialized broadcast payloads, atomic metrics counters, and asynchronous logging. The remaining likely bottlenecks are JSON parsing cost, large room fan-out, and slow client output buffers. Those are acceptable for this stage because the protocol is still intentionally simple and the broadcast path no longer holds shared-state locks while sending.

## Build

This muduo tree is Linux-oriented, so build the server and CLI tools on Linux or WSL. Install CMake, compiler tools, Boost headers, and SQLite development headers first, for example `sudo apt install cmake build-essential libboost-dev sqlite3 libsqlite3-dev` on Ubuntu-like systems.

```sh
cmake -S . -B build-stage2 -DCMAKE_BUILD_TYPE=Debug
cmake --build build-stage2
ctest --test-dir build-stage2 --output-on-failure
```

Build the Qt GUI client separately with Qt 6:

```sh
cmake -S qt-client -B build-qt-client -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt-client
```

On Windows, pass your Qt install path if CMake cannot find it:

```bat
cmake -S qt-client -B build-qt-client -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2019_64
cmake --build build-qt-client --config Release
```

## SQLite Persistence

The server stores private and room messages in SQLite through `server/ChatStorage.*`. The default database path is `chat_history.sqlite3` relative to the server process working directory.

You can change the database path with either:

```sh
CHATSERVICE_DB_PATH=/var/lib/chatservice/chat_history.sqlite3 ./build-stage2/bin/chat_server
./build-stage2/bin/chat_server --db /var/lib/chatservice/chat_history.sqlite3
```

SQLite WAL mode and `synchronous=NORMAL` are enabled at startup. Private conversations use normalized ids in the form `private:min_user:max_user`, so `alice` to `bob` and `bob` to `alice` load the same history thread.

For a safe offline export, stop the server or point at a stable database copy, then run:

```sh
./build-stage2/bin/chat_server --db chat_history.sqlite3 --dump-history history_dump.json
```

## Stress Test

The stress tool is a C++ executable that uses `std::thread` and POSIX sockets. By default it starts 1000 concurrent clients. Each client connects, logs in as `stress_N`, and sends one self-addressed private message every second.

```sh
./build-stage2/bin/stress_client --clients 1000 --duration 60 --csv stress_results.csv
```

Useful options:

```text
--clients N
--duration SECONDS
--host HOST
--port PORT
--csv PATH
```

The CSV contains:

```text
clients,duration_seconds,elapsed_seconds,connection_attempts,successful_connections,failed_connections,connection_success_rate,messages_sent,messages_received,average_latency_us,throughput_messages_per_second
```

For a quick smoke test on a laptop, run fewer clients:

```sh
./build-stage2/bin/stress_client --clients 20 --duration 5 --csv stress_results_smoke.csv
```

## Run

Start the server in one terminal:

```sh
./build-stage2/bin/chat_server
```

Start one or more clients in other terminals:

```sh
./build-stage2/bin/chat_client
```

The Qt GUI client defaults to `47.109.187.23:8888` from `qt-client/src/ClientConfig.h`. The login page hides host and port by default; use Advanced Settings to override them.

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
