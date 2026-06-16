cmake_minimum_required(VERSION 3.16)

project(ChatGuiClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

add_executable(chat_gui_client
  src/main.cpp
  src/Protocol.h
  src/Protocol.cpp
  src/ChatClient.h
  src/ChatClient.cpp
  src/LoginWindow.h
  src/LoginWindow.cpp
  src/MainWindow.h
  src/MainWindow.cpp
)

target_link_libraries(chat_gui_client PRIVATE
  Qt6::Core
  Qt6::Widgets
  Qt6::Network
)

target_include_directories(chat_gui_client PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
