# Qt Chat GUI Client

This directory contains a native Qt6 Widgets desktop client for the existing muduo chat server. It connects to `127.0.0.1:8888` by default and uses the same line-delimited JSON protocol as the CLI client.

## Features

- Login with host, port, and username.
- Refresh online users.
- Send and receive private messages.
- Create, join, leave, and use chat rooms.
- Automatically join rooms created by this GUI client.
- Maintain a local room list for rooms joined by this GUI client.
- Handle partial TCP packets and multiple JSON lines per read.

## Requirements

- CMake 3.16 or newer.
- Qt 6 with `Core`, `Widgets`, and `Network`.
- A running ChatService muduo server.

## Build On Windows

Install Qt 6 with the matching compiler kit for your environment, then configure CMake with the Qt prefix path. Example for MSVC:

```bat
cmake -S qt-client -B build-qt-client -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2019_64
cmake --build build-qt-client --config Release
```

If Qt's CMake package is already on `PATH`, the prefix path may not be needed.

## Build On Linux

Install Qt 6 development packages, then build:

```sh
cmake -S qt-client -B build-qt-client -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt-client
```

On Ubuntu-like systems, the package is usually named `qt6-base-dev`.

## Run

Start the muduo server first from the repository root on Linux or WSL:

```sh
./build-stage2/bin/chat_server
```

Start the GUI client:

```sh
./build-qt-client/chat_gui_client
```

On multi-config generators such as Visual Studio, the executable is usually under:

```text
build-qt-client\Release\chat_gui_client.exe
```

## Protocol Notes

Every outgoing message is exactly one flat JSON object followed by `\n`. All JSON values are strings. The GUI uses `QTcpSocket`, keeps incomplete data in an internal buffer, and parses each completed line independently.

The server sends `{"type":"ok","message":"connected"}` immediately after TCP connection. The GUI treats that only as connection information. It opens the main chat window only after receiving an `ok` response whose message starts with `logged in as `.

## Current Limitations

- The server has no real room-list API such as `{"type":"rooms"}`.
- The room list is local only and contains rooms joined by the current GUI client.
- Room list contents are not restored after reconnecting.
- Chat history is in memory only.
