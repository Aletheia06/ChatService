# ChatService

Stage 0 is a small muduo-based TCP echo chat skeleton. It contains a server, an interactive command-line client, a shared configuration header, and a minimal CTest target.

## Architecture

- `server/` contains the muduo `TcpServer` wrapper and the server entry point.
- `client/` contains the muduo `TcpClient` wrapper and the interactive command-line entry point.
- `common/` contains shared constants such as the loopback host and port.
- `tests/` contains lightweight build-time checks for shared configuration.
- `muduo/` is the local muduo source dependency used by the top-level CMake project.

The server binds to `127.0.0.1:8888`. muduo accepts multiple TCP clients and invokes the message callback for each connection. Stage 0 keeps the protocol deliberately simple: each received byte sequence is sent back unchanged.

## Directory Structure

```text
ChatService/
├── CMakeLists.txt
├── README.md
├── client/
│   ├── ChatClient.cc
│   ├── ChatClient.h
│   └── main.cc
├── common/
│   └── Config.h
├── server/
│   ├── EchoServer.cc
│   ├── EchoServer.h
│   └── main.cc
├── tests/
│   ├── CMakeLists.txt
│   └── config_test.cc
└── muduo/
```

## Build

This muduo tree is Linux-oriented, so build this project on Linux or WSL.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

Start the server in one terminal:

```sh
./build/bin/chat_server
```

Start one or more clients in other terminals:

```sh
./build/bin/chat_client
```

Type a line in the client and press Enter. The client sends the line to the server and prints the echoed response. Type `/quit` or press Ctrl-D to exit the client.
