#ifndef CHATSERVICE_SERVER_CHATSTORAGE_H
#define CHATSERVICE_SERVER_CHATSTORAGE_H

#include <stdint.h>

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace chatservice
{

struct Message
{
  int64_t id;
  std::string conversationType;
  std::string conversationId;
  std::string sender;
  std::string receiver;
  std::string roomId;
  std::string content;
  int64_t createdAt;
};

class ChatStorage
{
 public:
  ChatStorage();
  ~ChatStorage();

  bool init(const std::string& dbPath);

  bool savePrivateMessage(const std::string& sender,
                          const std::string& receiver,
                          const std::string& content,
                          int64_t timestamp,
                          int64_t* insertedId = NULL);

  bool saveRoomMessage(const std::string& roomId,
                       const std::string& sender,
                       const std::string& content,
                       int64_t timestamp,
                       int64_t* insertedId = NULL);

  std::vector<Message> loadPrivateHistory(const std::string& userA,
                                          const std::string& userB,
                                          int limit,
                                          int64_t beforeTimestamp);

  std::vector<Message> loadRoomHistory(const std::string& roomId,
                                       int limit,
                                       int64_t beforeTimestamp);

  bool dumpAllMessagesJson(const std::string& outputPath,
                           std::string* error);

  static std::string privateConversationId(const std::string& userA,
                                           const std::string& userB);

 private:
  bool exec(const char* sql);
  void close();

  sqlite3* db_;
  std::mutex mutex_;
};

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_CHATSTORAGE_H
