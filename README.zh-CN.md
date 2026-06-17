# ChatService 中文说明

[English README](README.md)

ChatService 是一个基于 muduo 的 TCP 聊天系统，支持登录、在线用户列表、私聊、聊天室、房间消息和压力测试。服务端使用行分隔 JSON 协议，每一条 TCP 消息都是一个 JSON 对象加一个换行符 `\n`。

当前版本增加了 SQLite 持久化：私聊消息和房间消息会写入服务端数据库，客户端重新登录后可以按会话加载历史消息。

## 目录结构

```text
ChatService/
|-- CMakeLists.txt          # 服务端、命令行客户端、压力测试工具的 CMake 入口
|-- README.md
|-- README.zh-CN.md
|-- common/                 # 通用配置、JSON 和命令协议
|-- server/                 # muduo 聊天服务端
|-- client/                 # 命令行客户端
|-- tools/                  # stress_client 压力测试工具
|-- tests/                  # CTest 测试
|-- qt-client/              # Qt 6 Widgets 图形客户端
`-- muduo/                  # 项目内置 muduo 源码
```

## 服务端从 0 构建

服务端依赖 muduo，当前顶层工程面向 Linux / WSL 构建，不建议直接在 Windows 原生命令行里构建服务端。

以 Ubuntu / WSL 为例，先从 GitHub 克隆项目并安装依赖：

```sh
git clone <your-repo-url> ChatService
cd ChatService
sudo apt update
sudo apt install cmake build-essential libboost-dev sqlite3 libsqlite3-dev
```

然后在仓库根目录配置并编译：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

构建完成后，可执行文件在：

```text
build/bin/chat_server
build/bin/chat_client
build/bin/stress_client
```

启动服务端：

```sh
./build/bin/chat_server
```

如果要指定 SQLite 数据库文件位置，可以使用环境变量或命令行参数：

```sh
CHATSERVICE_DB_PATH=/var/lib/chatservice/chat_history.sqlite3 ./build/bin/chat_server
./build/bin/chat_server --db /var/lib/chatservice/chat_history.sqlite3
```

## 服务端监听地址和客户端连接地址

这两个概念必须分开：

- 服务端监听地址在 `common/Config.h` 里配置，例如云服务器上可以使用 `0.0.0.0` 监听所有网卡。
- Qt 客户端默认连接地址在 `qt-client/src/ClientConfig.h` 里配置，当前默认是 `47.109.187.23:8888`。

不要把服务端的 `kServerHost` 改成公网 IP。公网部署时，服务端通常应该绑定 `0.0.0.0`，客户端才连接公网 IP。

## Qt 图形客户端从 0 构建

Qt 客户端是独立的 Qt 6 Widgets 工程，目录是 `qt-client/`。它可以在 Windows 或 Linux 上构建。

### Windows 构建

先安装 Qt 6，并确保安装了与你的编译器匹配的套件，例如 MSVC 64-bit 或 MinGW 64-bit。

如果使用 MSVC 套件，示例命令如下：

```bat
git clone <your-repo-url> ChatService
cd ChatService
cmake -S qt-client -B build-qt-client -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\msvc2019_64
cmake --build build-qt-client --config Release
```

如果使用 MinGW 套件，示例命令如下，路径请按本机 Qt 安装目录调整：

```bat
git clone <your-repo-url> ChatService
cd ChatService
cmake -S qt-client -B build-qt-client -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:\Qt\6.7.0\mingw_64
cmake --build build-qt-client
```

多配置生成器的 exe 通常在：

```text
build-qt-client\Release\chat_gui_client.exe
```

MinGW Makefiles 等单配置生成器的 exe 通常在：

```text
build-qt-client\chat_gui_client.exe
```

### Linux 构建

安装 Qt 6 开发包：

```sh
sudo apt update
sudo apt install cmake build-essential qt6-base-dev
```

构建 Qt 客户端：

```sh
git clone <your-repo-url> ChatService
cd ChatService
cmake -S qt-client -B build-qt-client -DCMAKE_BUILD_TYPE=Release
cmake --build build-qt-client
```

运行：

```sh
./build-qt-client/chat_gui_client
```

## Qt 客户端使用方式

普通用户打开客户端后只需要输入用户名并登录。Host 和 Port 默认隐藏，默认连接 `47.109.187.23:8888`。

如果需要连接本地测试服务端或其他服务器，点击登录页的 `Advanced Settings`，手动修改 Host 和 Port。

登录后左侧包含：

- 当前用户信息；
- 在线用户列表；
- 已加入房间列表；
- 最近会话列表。

右侧是当前选中的会话：

- 每个私聊都有独立消息视图；
- 每个房间都有独立消息视图；
- 后台会话收到消息时会显示未读数量；
- 第一次打开私聊或房间时会自动向服务端请求历史消息；
- 当前打开的会话收到新消息时会自动滚动到底部。

## SQLite 持久化

服务端通过 `server/ChatStorage.*` 管理 SQLite，不把 SQL 分散到业务代码里。

默认数据库文件是：

```text
chat_history.sqlite3
```

它位于服务端进程当前工作目录下。例如你在仓库根目录执行 `./build/bin/chat_server`，数据库就会出现在仓库根目录。

启动时会启用：

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;
```

私聊会话 id 会归一化，例如 `alice` 和 `bob` 的私聊统一保存为：

```text
private:alice:bob
```

所以 `alice -> bob` 和 `bob -> alice` 会加载同一个历史线程。

如果需要导出历史消息，可以在服务端停止后或对稳定数据库副本执行：

```sh
./build/bin/chat_server --db chat_history.sqlite3 --dump-history history_dump.json
```

## JSON 协议简表

登录：

```json
{"type":"login","username":"alice"}
```

在线用户：

```json
{"type":"users"}
```

私聊：

```json
{"type":"private","target":"bob","message":"hello"}
```

创建、加入、离开房间和发送房间消息：

```json
{"type":"create_room","room":"lobby"}
{"type":"join","room":"lobby"}
{"type":"leave","room":"lobby"}
{"type":"room_msg","room":"lobby","message":"hello everyone"}
```

加载历史消息：

```json
{"type":"history_private","peer":"bob","limit":50}
{"type":"history_room","room":"lobby","limit":50}
```

历史响应示例：

```json
{"type":"history_private_result","peer":"bob","messages":[{"id":1,"sender":"bob","receiver":"alice","content":"hello","created_at":1710000000}]}
{"type":"history_room_result","room":"lobby","messages":[{"id":2,"sender":"alice","room":"lobby","content":"hello everyone","created_at":1710000000}]}
```

## 命令行客户端

构建服务端工程后，可以运行命令行客户端：

```sh
./build/bin/chat_client
```

支持命令：

```text
LOGIN alice
LOGOUT
USERS
PRIVATE bob hello
CREATE_ROOM lobby
JOIN lobby
LEAVE lobby
ROOM_MSG lobby hello everyone
HISTORY_PRIVATE bob 50
HISTORY_ROOM lobby 50
```

退出命令行客户端：

```text
/quit
```

## 压力测试

构建后运行：

```sh
./build/bin/stress_client --clients 1000 --duration 60 --csv stress_results.csv
```

本地快速测试可以减少并发数量：

```sh
./build/bin/stress_client --clients 20 --duration 5 --csv stress_results_smoke.csv
```

常用参数：

```text
--clients N
--duration SECONDS
--host HOST
--port PORT
--csv PATH
```
