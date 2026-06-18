# ChatService

**Language:** [English](README.md) | [中文](README.zh-CN.md)

Stage 2 is a muduo-based TCP chat system optimized for higher concurrency. It keeps the Stage 1 user, private-message, and room features, then adds a muduo worker thread pool, multiple `EventLoop` instances, lower-lock routing, asynchronous logging, and periodic performance statistics.

The server and client speak a simple line-delimited JSON protocol: each TCP message is one JSON object followed by `\n`.

## Architecture

- `server/ChatServer.*` integrates muduo `EventLoop` and `TcpServer`, configures the worker event-loop pool, owns connection lifecycle, and routes requests.
- `server/ChatSession.*` stores per-connection state, including the logged-in username.
- `server/ChatStorage.*` owns SQLite persistence, history queries, and JSON history export.
- `server/CallManager.*` owns the mutex-protected one-to-one call state machine.
- `server/UserManager.*` maintains the thread-safe online user map and supports one-lock connection snapshots for broadcasts.
- `server/RoomManager.*` maintains thread-safe room membership and returns member-name snapshots.
- `server/ServerMetrics.*` records online users, message rate, average latency, total connections, and dropped connections.
- `client/` contains the muduo `TcpClient` wrapper and an interactive command-line client.
- `qt-client/` contains the Qt 6 Widgets GUI client.
- `common/Json.*` implements the small JSON object parser and serializer used by the wire protocol.
- `common/Protocol.*` translates CLI commands into JSON requests and attaches client send timestamps for latency measurement.
- `tests/` contains focused CTest checks for configuration and protocol behavior.
- `ws-gateway/` contains a Node.js WebSocket-to-TCP gateway for browser clients.
- `web-client/` contains a plain HTML/CSS/JavaScript browser client.
- `web-client/webrtc.js` owns browser media capture and `RTCPeerConnection` behavior.

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
|-- web-client/
|   |-- app.js
|   |-- index.html
|   `-- style.css
|-- ws-gateway/
|   |-- config.js
|   |-- package.json
|   `-- server.js
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

## WebSocket Gateway And Web Client

The browser client is an external layer around the existing C++ server. It does not change the TCP JSON-line protocol:

```text
Browser Web Client
  -> WebSocket
Node.js WebSocket Gateway
  -> TCP JSON line, one JSON object followed by \n
Existing C++/muduo chat_server
```

The gateway creates one TCP connection to the C++ server for each browser WebSocket connection. Browser messages are parsed as JSON objects, serialized back to compact JSON, and sent to the TCP server with a trailing newline. TCP responses are buffered and split by newline before each complete JSON object is forwarded to the browser, so sticky packets and partial packets are handled at the gateway boundary.

By default the gateway listens on `127.0.0.1:9000` and connects to the chat server at `127.0.0.1:8888`. Override these values with environment variables:

```sh
WS_HOST=127.0.0.1 WS_PORT=9000 CHAT_TCP_HOST=127.0.0.1 CHAT_TCP_PORT=8888 npm start
```

The web client reuses the existing request and response names: `login`, `logout`, `users`, `private`, `create_room`, `join`, `leave`, `room_msg`, `history_private`, and `history_room`. The server has no room-list API, so the browser keeps joined rooms locally, just like the Qt client.

When `web-client/` is served over HTTP or HTTPS, the browser login page derives its WebSocket URL from the current page host:

```text
http://chat.example.com  ->  ws://chat.example.com/ws
https://chat.example.com ->  wss://chat.example.com/ws
```

This keeps the static frontend deployment-friendly. Do not hardcode a public server IP into the browser files. If you open `web-client/index.html` directly from disk for local testing, the fallback Gateway value is `ws://localhost:9000`.

### One-to-One WebRTC Video Calls

The web client supports one-to-one video calls. Audio and video do not pass
through the C++ server or Node.js gateway:

```text
Browser A <-- WebRTC audio/video --> Browser B
    |                                  |
    +-- WebSocket/TCP signaling -------+
```

The C++ server forwards only call control, SDP, and ICE messages:

```text
call_invite
call_accept
call_reject
call_busy
call_cancel
call_hangup
call_timeout
call_error
webrtc_offer
webrtc_answer
ice_candidate
```

Every signaling request includes `to` and `call_id`. The server ignores any
client-supplied `from` value and uses the authenticated session username.
Examples:

```json
{"type":"call_invite","to":"bob","call_id":"550e8400-e29b-41d4-a716-446655440000","media":"video"}
{"type":"call_accept","to":"alice","call_id":"550e8400-e29b-41d4-a716-446655440000"}
{"type":"webrtc_offer","to":"bob","call_id":"550e8400-e29b-41d4-a716-446655440000","sdp":{"type":"offer","sdp":"..."}}
{"type":"ice_candidate","to":"bob","call_id":"550e8400-e29b-41d4-a716-446655440000","candidate":{"candidate":"...","sdpMid":"0","sdpMLineIndex":0}}
```

`CallManager` gives each non-idle user one state:

- `calling`: caller is waiting for an answer.
- `ringing`: callee has an incoming invitation.
- `in_call`: the invitation was accepted and WebRTC signaling is allowed.
- `idle`: represented by no active entry.

Starting a call atomically reserves both users. This allows independent pairs,
such as Alice/Bob and Charlie/David, while preventing any user from joining two
calls. Accept/reject/cancel/hangup operations must match the current peer and
`call_id`. SDP and ICE forwarding is allowed only for two users in the same
accepted call. Logout, TCP/WebSocket disconnect, and browser refresh clear the
server state and notify the remaining peer.

The browser uses a 30-second ringing timer. After acceptance, the caller sends
the offer, the callee sends the answer, and both sides exchange ICE candidates.
The development ICE configuration is centralized in `web-client/webrtc.js`:

```js
const RTC_CONFIG = {
  iceServers: [
    { urls: 'stun:stun.l.google.com:19302' }
  ]
};
```

#### Local Browser Test

1. Build and start the C++ server:

   ```sh
   ./build/bin/chat_server
   ```

2. Start the WebSocket gateway:

   ```sh
   cd ws-gateway
   npm ci
   npm start
   ```

3. Serve the browser files instead of opening them directly:

   ```sh
   python3 -m http.server 8080 --directory web-client
   ```

4. Open `http://localhost:8080` in two tabs. In Advanced Settings set the
   gateway to `ws://localhost:9000`, then log in as `alice` and `bob`.
5. Click `Video Call` next to Bob, accept in Bob's tab, grant camera/microphone
   access, verify both videos, and hang up.

Also verify rejection, the 30-second timeout, denied media permission, a third
user receiving `User is busy`, and two independent pairs calling at the same
time. Use five tabs (`alice`, `bob`, `charlie`, `david`, `eve`) for the full
two-call scenario.

#### Production Requirements

Camera and microphone APIs require a secure context. `localhost` is normally
allowed for development, but public deployments must serve the page over
HTTPS. When the page uses HTTPS, signaling must use WSS; the existing frontend
selects `wss://<current-host>/ws`, and the provided Nginx configuration proxies
that path to the loopback gateway.

The public STUN server is suitable for development but does not guarantee
connectivity through restrictive or symmetric NAT. Add a TURN service to
`RTC_CONFIG.iceServers` before production, preferably with short-lived
credentials. This MVP has no SFU/MCU or media relay other than a future TURN
fallback, so it remains strictly one-to-one.

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

## Build From A Fresh GitHub Clone

The muduo server tree is Linux-oriented, so build the server, CLI client, and stress tool on Linux or WSL. From a fresh GitHub checkout:

```sh
git clone <your-repo-url> ChatService
cd ChatService
sudo apt update
sudo apt install cmake build-essential libboost-dev sqlite3 libsqlite3-dev
```

Configure and build the server-side project:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

After this, the generated binaries are under `build/bin/`:

```text
build/bin/chat_server
build/bin/chat_client
build/bin/stress_client
```

Build the Qt GUI client separately with Qt 6. On Linux:

```sh
cmake -S qt-client -B build-qt-client -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt-client
```

On Windows, install Qt 6 first, then pass your Qt install path if CMake cannot find it:

```bat
cmake -S qt-client -B build-qt-client -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2019_64
cmake --build build-qt-client --config Release
```

Install the WebSocket gateway dependencies with Node.js 18 or newer:

```sh
cd ws-gateway
npm install
cd ..
```

## SQLite Persistence

The server stores private and room messages in SQLite through `server/ChatStorage.*`. The default database path is `chat_history.sqlite3` relative to the server process working directory.

You can change the database path with either:

```sh
CHATSERVICE_DB_PATH=/var/lib/chatservice/chat_history.sqlite3 ./build/bin/chat_server
./build/bin/chat_server --db /var/lib/chatservice/chat_history.sqlite3
```

SQLite WAL mode and `synchronous=NORMAL` are enabled at startup. Private conversations use normalized ids in the form `private:min_user:max_user`, so `alice` to `bob` and `bob` to `alice` load the same history thread.

For a safe offline export, stop the server or point at a stable database copy, then run:

```sh
./build/bin/chat_server --db chat_history.sqlite3 --dump-history history_dump.json
```

## Stress Test

The stress tool is a C++ executable that uses `std::thread` and POSIX sockets. By default it starts 1000 concurrent clients. Each client connects, logs in as `stress_N`, and sends one self-addressed private message every second.

```sh
./build/bin/stress_client --clients 1000 --duration 60 --csv stress_results.csv
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
./build/bin/stress_client --clients 20 --duration 5 --csv stress_results_smoke.csv
```

## Run

Start the server in one terminal:

```sh
./build/bin/chat_server
```

Start one or more CLI clients in other terminals:

```sh
./build/bin/chat_client
```

The Qt GUI client defaults to `47.109.187.23:8888` from `qt-client/src/ClientConfig.h`. The login page hides host and port by default; use Advanced Settings to override them.

Start the WebSocket gateway in another terminal:

```sh
cd ws-gateway
npm start
```

With the default configuration, the gateway listens on `ws://127.0.0.1:9000` and connects to `127.0.0.1:8888` for the C++ server. In production, Nginx exposes the gateway through `/ws`; port 9000 remains private.

Open the browser client locally by opening:

```text
web-client/index.html
```

When opened from disk, the login page falls back to `ws://localhost:9000` in Advanced Settings. When served through a domain, it automatically uses the current host plus `/ws`, such as `wss://chat.example.com/ws`.

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

## Production Deployment: aletheia6.top

The production browser client is served from:

```text
Domain:   https://aletheia6.top
Web root: /var/www/aletheia-chat
Gateway:  127.0.0.1:9000
Server:   127.0.0.1:8888 from the gateway
```

The C++ server remains available on public TCP port 8888 for the Qt desktop client. Browser traffic never connects to that raw port. Nginx serves the static files and upgrades `/ws` requests before proxying them to the loopback-only Node.js gateway:

```text
Browser -> HTTPS / WSS -> Nginx -> 127.0.0.1:9000 -> 127.0.0.1:8888
Qt client -----------------------------------------------> public TCP :8888
```

Deployment templates are stored under `deploy/`:

- `deploy/aletheia-chat.nginx` is the Nginx HTTPS server block and HTTP redirect.
- `deploy/chat-server.service` keeps the existing C++ server running through systemd.
- `deploy/ecosystem.config.cjs` keeps the WebSocket gateway running through PM2.

Manage the C++ server:

```sh
systemctl status chat-server
systemctl restart chat-server
journalctl -u chat-server -n 100 --no-pager
```

Manage the WebSocket gateway:

```sh
pm2 status
pm2 restart chat-ws-gateway
pm2 logs chat-ws-gateway --lines 100
pm2 save
```

Validate and reload Nginx:

```sh
nginx -t
systemctl reload nginx
```

Test the deployment:

```sh
curl -I https://aletheia6.top/
curl -I https://www.aletheia6.top/
systemctl is-active chat-server nginx
pm2 status
ss -ltnp | grep -E ':(80|443|8888|9000)\b'
```

HTTPS is managed by Certbot's Nginx integration. Certificates cover both `aletheia6.top` and `www.aletheia6.top`, HTTP redirects to HTTPS, and the unchanged frontend automatically switches from `ws://.../ws` to `wss://.../ws`.
