cmake_minimum_required(VERSION 3.16)

project(ChatService LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "This muduo tree is Linux-oriented. Build this project on Linux or WSL.")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MUDUO_BUILD_EXAMPLES "Build muduo examples" OFF)
option(CHATSERVICE_BUILD_TESTS "Build ChatService tests" ON)

add_subdirectory(muduo)

add_library(chat_common
  common/Json.cc
  common/Protocol.cc
)
target_include_directories(chat_common PUBLIC
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/muduo
)

add_executable(chat_server
  server/ChatServer.cc
  server/ChatSession.cc
  server/RoomManager.cc
  server/UserManager.cc
  server/main.cc
)
target_link_libraries(chat_server PRIVATE chat_common muduo_net)

add_executable(chat_client
  client/ChatClient.cc
  client/main.cc
)
target_link_libraries(chat_client PRIVATE chat_common muduo_net)

set_target_properties(chat_server chat_client PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
)

if(CHATSERVICE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
