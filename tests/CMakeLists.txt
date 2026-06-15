add_executable(chat_config_test
  config_test.cc
)
target_link_libraries(chat_config_test PRIVATE chat_common)

add_test(NAME chat_config_test COMMAND chat_config_test)
