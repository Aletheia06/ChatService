# ChatService stress test

This directory contains a headless Python 3 benchmark for the C++ chat server. It connects directly to the server's newline-delimited TCP JSON protocol on port `8888`; it does not use the Qt client, web frontend, or WebSocket gateway.

The benchmark uses the exact server request fields:

- Login: `{"type":"login","username":"..."}`
- Private message: `{"type":"private","target":"...","message":"..."}`
- Create room: `{"type":"create_room","room":"..."}`
- Join room: `{"type":"join","room":"..."}`
- Room message: `{"type":"room_msg","room":"...","message":"..."}`

Every test message embeds a run id, message id, monotonic send timestamp, sender sequence, sender, and destination in the message body. The server relays that body unchanged, allowing the client to measure end-to-end latency, delivery loss, duplicates, incorrect routing, and per-sender ordering.

## Requirements

- Python 3.10 or newer is recommended.
- No third-party Python package is required.
- `psutil` is used when available for `--server-pid` resource monitoring. On Linux, the tool falls back to `/proc`.
- The C++ server must be running and reachable from the stress-test machine.

## Start the chat server

The C++ project is Linux/WSL-oriented. From the repository root:

```sh
cmake -S . -B build -DCHATSERVICE_BUILD_TESTS=ON
cmake --build build -j
./build/bin/chat_server --db /tmp/chatservice-benchmark.sqlite3
```

If an existing build directory is current, start its `bin/chat_server` instead.

Use a dedicated benchmark database. Private and room messages are stored in SQLite, so a long benchmark can create a large database and storage I/O may become the limiting factor. Stop the server before deleting the test database and its `-wal`/`-shm` files.

The server currently logs periodic metrics such as online users, handled requests per second, average request latency, total messages, total connections, and closed connections. The Python benchmark adds end-to-end latency, delivery correctness, and optional CPU/RSS sampling.

## Run one test

From the repository root:

```sh
python3 stress-test/stress_test.py \
  --host 127.0.0.1 \
  --port 8888 \
  --users 1000 \
  --rooms 10 \
  --duration 120 \
  --message-rate 1 \
  --mode room
```

Windows PowerShell uses the same arguments:

```powershell
python .\stress-test\stress_test.py --host 127.0.0.1 --port 8888 --users 100 --rooms 10 --duration 60 --message-rate 1 --mode room
```

Supported modes:

- `connect_only`: connect and log in, then hold the connections open.
- `private`: every active user sends to another randomly selected live user.
- `room`: all active users join one of the requested rooms and send room messages.
- `mixed`: approximately 40% room senders, 40% private senders, and 20% idle users.

`--message-rate` is per sending user. In room mode, one sent message produces one delivery for every live member of that room. For example, 1,000 users split across 10 rooms at 1 message/second requests roughly 100,000 room deliveries/second, not 1,000.

Useful reliability and scaling options:

```text
--connect-timeout SECONDS
--operation-timeout SECONDS
--send-timeout SECONDS
--grace-period SECONDS
--connect-concurrency N       # 0 means all connection attempts may overlap
--max-latency-samples N       # bounded reservoir used for p50/p95/p99
--results-dir PATH
--result-name NAME
--log-level DEBUG|INFO|WARNING|ERROR
```

The logs written to the terminal are JSON lines. Per-message logging is deliberately avoided because it would distort benchmark results.

## CPU and memory monitoring

Pass the server process id when the stress tool runs on the same operating-system instance as the server:

```sh
SERVER_PID=$(pgrep -n chat_server)
python3 stress-test/stress_test.py \
  --users 500 --rooms 10 --duration 60 --message-rate 1 --mode room \
  --server-pid "$SERVER_PID"
```

Resource samples are saved to `latest_resource_samples.csv`. The summary reports average/max process CPU and RSS. CPU is reported in process-percent units, so a multithreaded process may exceed 100%.

Do not pass a Windows PID for a server running inside WSL, or a WSL PID to a benchmark running as a Windows process. Run the benchmark in the same environment as the server if resource sampling is required.

## Output files

Every run atomically replaces:

```text
stress-test/results/latest_summary.json
stress-test/results/latest_summary.csv
stress-test/results/latest_latency_samples.csv
stress-test/results/latest_resource_samples.csv
```

`--result-name smoke_10u` also writes `smoke_10u_summary.json`, `smoke_10u_summary.csv`, `smoke_10u_latency_samples.csv`, and `smoke_10u_resource_samples.csv`.

Important fields:

- `total_messages_sent`: application chat requests successfully written by virtual users.
- `expected_message_deliveries`: expected live events after private delivery or room fan-out.
- `unique_message_deliveries`: valid, non-duplicate benchmark events received.
- `message_loss_rate_percent`: `(expected - unique) / expected`, clamped at zero.
- `sent_throughput_messages_per_second`: chat requests sent per second.
- `received_throughput_deliveries_per_second`: valid fan-out deliveries received per second.
- `average_latency_ms`: exact streaming average over every valid received delivery.
- `p50_latency_ms`, `p95_latency_ms`, `p99_latency_ms`: calculated from the bounded latency reservoir.
- `latency_percentiles_are_sampled`: `true` when more deliveries were seen than retained.
- `unexpected_disconnect_count`, `duplicate_delivery_count`, `out_of_order_delivery_count`, and `incorrect_delivery_count`: correctness indicators.

The benchmark marks a result `laggy` when any requested threshold is crossed:

- p95 latency greater than 1,000 ms
- p99 latency greater than 2,000 ms
- message loss greater than 1%
- connection failure greater than 1%
- sustained server CPU above the configured threshold
- unexpected disconnects
- duplicate, out-of-order, or incorrectly routed deliveries

## Run a concurrency matrix

The default matrix tests `10,50,100,200,500,1000,2000` users:

```sh
python3 stress-test/run_matrix.py \
  --host 127.0.0.1 \
  --port 8888 \
  --duration 60 \
  --rooms 10 \
  --message-rate 1 \
  --mode room
```

Use a smaller exploratory matrix first:

```sh
python3 stress-test/run_matrix.py \
  --levels 10,50,100,200 \
  --duration 30 \
  --rooms 10 \
  --message-rate 0.2 \
  --mode room
```

The matrix keeps named files for every level and creates:

```text
stress-test/results/latest_matrix_summary.json
stress-test/results/latest_matrix_summary.csv
```

The first laggy row is the first observed failure point. The previous healthy row is a practical lower-bound estimate, not a universal capacity number. Repeat runs and use finer levels around the transition before publishing a limit.

## Recommended test plan

1. Run `connect_only` to find the connection/login ceiling without message traffic.
2. Run a low-rate room test, such as `--message-rate 0.1`.
3. Run a high-rate room test, increasing rate while keeping the same room layout.
4. Run `mixed` to approximate a more realistic blend of private, room, and idle users.
5. Find the first concurrency level where p95 latency exceeds 1,000 ms or message loss exceeds 1%; also reject levels with connection failures, disconnects, ordering errors, or sustained CPU saturation.
6. Repeat the boundary levels at least three times and compare medians.

For each phase, use a dedicated database and record the server build, machine size, worker-thread count, room count, message rate, and whether client and server shared a host.

## Common mistakes

- Running the server and stress client on the same small machine. Client CPU, memory, socket buffers, and loopback behavior can hide or create the bottleneck.
- Treating a room send as one unit of server work. Room fan-out grows with room membership.
- Reusing a production database. The benchmark creates real stored messages.
- Starting with 2,000 users at a high room rate. Increase one dimension at a time.
- Ignoring client saturation. Watch stress-client CPU and network usage as well as the server.
- Using too few file descriptors on Linux. Check `ulimit -n` before large connection tests.
- Testing across the public internet when the goal is server capacity. Network latency and loss then become part of the result.
- Comparing runs with different room sizes, SQLite storage, server builds, or worker counts.

## Small smoke test

The requested local smoke shape is:

```sh
python3 stress-test/stress_test.py \
  --host 127.0.0.1 \
  --port 8888 \
  --users 10 \
  --rooms 2 \
  --duration 30 \
  --message-rate 1 \
  --mode room \
  --result-name smoke_10u
```

This validates protocol correctness and result generation. It does not establish cloud capacity; the real cloud server still needs matrix runs from a separate load-generator machine.
