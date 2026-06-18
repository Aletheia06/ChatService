add_executable(chat_config_test
  config_test.cc
)
target_link_libraries(chat_config_test PRIVATE chat_common)

add_test(NAME chat_config_test COMMAND chat_config_test)

add_executable(chat_protocol_test
  protocol_test.cc
)
target_link_libraries(chat_protocol_test PRIVATE chat_common)

add_test(NAME chat_protocol_test COMMAND chat_protocol_test)

add_executable(chat_call_manager_test
  call_manager_test.cc
  ${PROJECT_SOURCE_DIR}/server/CallManager.cc
)
target_include_directories(chat_call_manager_test PRIVATE
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/muduo
)
target_link_libraries(chat_call_manager_test PRIVATE muduo_base)

add_test(NAME chat_call_manager_test COMMAND chat_call_manager_test)
