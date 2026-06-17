# Qt Chat GUI Client

This directory contains a native Qt6 Widgets desktop client for the existing muduo chat server. It connects to `47.109.187.23:8888` by default and uses the same line-delimited JSON protocol as the CLI client.

## Features

- Login with username only for normal use.
- Edit host and port through Advanced Settings when needed.
- Refresh online users.
- Keep private chats in separate conversation views.
- Create, join, leave, and use chat rooms.
- Keep each room in its own conversation view.
- Show unread counts for conversations that receive messages in the background.
- Load private and room history from the server the first time a conversation is opened.
- Automatically join rooms created by this GUI client.
- Maintain a local room list for rooms joined by this GUI client.
- Handle partial TCP packets and multiple JSON lines per read.

## UI

The client uses Qt Widgets only. The visual style is centralized in a small QSS helper and keeps a clean light theme with a clear sidebar, rounded inputs, rounded buttons, and simple message bubbles.

The left side contains current user status, online users, joined rooms, and recent conversations. The right side shows the selected conversation title, only that conversation's messages, the input box, and the send button.

Private messages, room messages, system messages, and error messages use different bubble styles. Outgoing messages are aligned to the right, incoming messages are aligned to the left, and system or error messages are centered.

Rooms are still local-only because the server currently has no room-list API.

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

The default connection endpoint lives in `src/ClientConfig.h`:

```cpp
namespace ClientConfig {
constexpr const char* DEFAULT_SERVER_HOST = "47.109.187.23";
constexpr quint16 DEFAULT_SERVER_PORT = 8888;
}
```

This is intentionally separate from the server bind host in `common/Config.h`.

## Protocol Notes

Every outgoing message is one JSON object followed by `\n`. The GUI uses `QTcpSocket`, keeps incomplete data in an internal buffer, and parses each completed line independently.

The server sends `{"type":"ok","message":"connected"}` immediately after TCP connection. The GUI treats that only as connection information. It opens the main chat window only after receiving an `ok` response whose message starts with `logged in as `.

When a private or room conversation is opened for the first time, the GUI sends `history_private` or `history_room` with a default limit of 50. Server history responses contain a `messages` array. Live events and history entries include database ids when they come from the SQLite-backed server, and the client uses those ids to avoid duplicate rendering.

## Current Limitations

- The server has no real room-list API such as `{"type":"rooms"}`.
- The room list is local only and contains rooms joined by the current GUI client.
- Room list contents are not restored after reconnecting.
