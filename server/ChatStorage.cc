#include "server/ChatStorage.h"

#include "common/Json.h"

#include <sqlite3.h>

#include <algorithm>
#include <fstream>
#include <sstream>

namespace chatservice
{
namespace
{

const int kDefaultHistoryLimit = 50;
const int kMaxHistoryLimit = 500;

int sanitizeLimit(int limit)
{
  if (limit <= 0)
  {
    return kDefaultHistoryLimit;
  }
  if (limit > kMaxHistoryLimit)
  {
    return kMaxHistoryLimit;
  }
  return limit;
}

std::string columnText(sqlite3_stmt* statement, int column)
{
  const unsigned char* text = sqlite3_column_text(statement, column);
  if (text == NULL)
  {
    return std::string();
  }
  return reinterpret_cast<const char*>(text);
}

void bindText(sqlite3_stmt* statement, int index, const std::string& value)
{
  sqlite3_bind_text(statement,
                    index,
                    value.c_str(),
                    static_cast<int>(value.size()),
                    SQLITE_TRANSIENT);
}

void writeJsonField(std::ostream& stream,
                    const std::string& name,
                    const std::string& value,
                    bool* first)
{
  if (!*first)
  {
    stream << ",";
  }
  *first = false;
  stream << "\"" << escapeJsonString(name) << "\":"
         << "\"" << escapeJsonString(value) << "\"";
}

void writeJsonIntField(std::ostream& stream,
                       const std::string& name,
                       int64_t value,
                       bool* first)
{
  if (!*first)
  {
    stream << ",";
  }
  *first = false;
  stream << "\"" << escapeJsonString(name) << "\":" << value;
}

Message messageFromStatement(sqlite3_stmt* statement)
{
  Message message;
  message.id = sqlite3_column_int64(statement, 0);
  message.conversationType = columnText(statement, 1);
  message.conversationId = columnText(statement, 2);
  message.sender = columnText(statement, 3);
  message.receiver = columnText(statement, 4);
  message.roomId = columnText(statement, 5);
  message.content = columnText(statement, 6);
  message.createdAt = sqlite3_column_int64(statement, 7);
  return message;
}

}  // namespace

ChatStorage::ChatStorage()
  : db_(NULL)
{
}

ChatStorage::~ChatStorage()
{
  close();
}

bool ChatStorage::init(const std::string& dbPath)
{
  std::lock_guard<std::mutex> lock(mutex_);
  close();

  if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK)
  {
    close();
    return false;
  }

  sqlite3_busy_timeout(db_, 5000);

  if (!exec("PRAGMA journal_mode=WAL;") ||
      !exec("PRAGMA synchronous=NORMAL;") ||
      !exec("CREATE TABLE IF NOT EXISTS messages ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "conversation_type TEXT NOT NULL,"
            "conversation_id TEXT NOT NULL,"
            "sender TEXT NOT NULL,"
            "receiver TEXT,"
            "room_id TEXT,"
            "content TEXT NOT NULL,"
            "created_at INTEGER NOT NULL"
            ");") ||
      !exec("CREATE INDEX IF NOT EXISTS idx_messages_conversation_time "
            "ON messages(conversation_type, conversation_id, created_at);"))
  {
    close();
    return false;
  }

  return true;
}

bool ChatStorage::savePrivateMessage(const std::string& sender,
                                     const std::string& receiver,
                                     const std::string& content,
                                     int64_t timestamp,
                                     int64_t* insertedId)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == NULL)
  {
    return false;
  }

  sqlite3_stmt* statement = NULL;
  const char* sql =
      "INSERT INTO messages "
      "(conversation_type, conversation_id, sender, receiver, room_id, "
      "content, created_at) "
      "VALUES (?, ?, ?, ?, NULL, ?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, NULL) != SQLITE_OK)
  {
    return false;
  }

  bindText(statement, 1, "private");
  bindText(statement, 2, privateConversationId(sender, receiver));
  bindText(statement, 3, sender);
  bindText(statement, 4, receiver);
  bindText(statement, 5, content);
  sqlite3_bind_int64(statement, 6, timestamp);

  const bool ok = sqlite3_step(statement) == SQLITE_DONE;
  if (ok && insertedId != NULL)
  {
    *insertedId = sqlite3_last_insert_rowid(db_);
  }
  sqlite3_finalize(statement);
  return ok;
}

bool ChatStorage::saveRoomMessage(const std::string& roomId,
                                  const std::string& sender,
                                  const std::string& content,
                                  int64_t timestamp,
                                  int64_t* insertedId)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == NULL)
  {
    return false;
  }

  sqlite3_stmt* statement = NULL;
  const char* sql =
      "INSERT INTO messages "
      "(conversation_type, conversation_id, sender, receiver, room_id, "
      "content, created_at) "
      "VALUES (?, ?, ?, NULL, ?, ?, ?);";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, NULL) != SQLITE_OK)
  {
    return false;
  }

  bindText(statement, 1, "room");
  bindText(statement, 2, roomId);
  bindText(statement, 3, sender);
  bindText(statement, 4, roomId);
  bindText(statement, 5, content);
  sqlite3_bind_int64(statement, 6, timestamp);

  const bool ok = sqlite3_step(statement) == SQLITE_DONE;
  if (ok && insertedId != NULL)
  {
    *insertedId = sqlite3_last_insert_rowid(db_);
  }
  sqlite3_finalize(statement);
  return ok;
}

std::vector<Message> ChatStorage::loadPrivateHistory(
    const std::string& userA,
    const std::string& userB,
    int limit,
    int64_t beforeTimestamp)
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Message> messages;
  if (db_ == NULL)
  {
    return messages;
  }

  sqlite3_stmt* statement = NULL;
  const char* sql =
      "SELECT id, conversation_type, conversation_id, sender, receiver, "
      "room_id, content, created_at "
      "FROM messages "
      "WHERE conversation_type = ? AND conversation_id = ? "
      "AND (? <= 0 OR created_at < ?) "
      "ORDER BY created_at DESC, id DESC "
      "LIMIT ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, NULL) != SQLITE_OK)
  {
    return messages;
  }

  bindText(statement, 1, "private");
  bindText(statement, 2, privateConversationId(userA, userB));
  sqlite3_bind_int64(statement, 3, beforeTimestamp);
  sqlite3_bind_int64(statement, 4, beforeTimestamp);
  sqlite3_bind_int(statement, 5, sanitizeLimit(limit));

  while (sqlite3_step(statement) == SQLITE_ROW)
  {
    messages.push_back(messageFromStatement(statement));
  }
  sqlite3_finalize(statement);
  std::reverse(messages.begin(), messages.end());
  return messages;
}

std::vector<Message> ChatStorage::loadRoomHistory(const std::string& roomId,
                                                  int limit,
                                                  int64_t beforeTimestamp)
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Message> messages;
  if (db_ == NULL)
  {
    return messages;
  }

  sqlite3_stmt* statement = NULL;
  const char* sql =
      "SELECT id, conversation_type, conversation_id, sender, receiver, "
      "room_id, content, created_at "
      "FROM messages "
      "WHERE conversation_type = ? AND conversation_id = ? "
      "AND (? <= 0 OR created_at < ?) "
      "ORDER BY created_at DESC, id DESC "
      "LIMIT ?;";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, NULL) != SQLITE_OK)
  {
    return messages;
  }

  bindText(statement, 1, "room");
  bindText(statement, 2, roomId);
  sqlite3_bind_int64(statement, 3, beforeTimestamp);
  sqlite3_bind_int64(statement, 4, beforeTimestamp);
  sqlite3_bind_int(statement, 5, sanitizeLimit(limit));

  while (sqlite3_step(statement) == SQLITE_ROW)
  {
    messages.push_back(messageFromStatement(statement));
  }
  sqlite3_finalize(statement);
  std::reverse(messages.begin(), messages.end());
  return messages;
}

bool ChatStorage::dumpAllMessagesJson(const std::string& outputPath,
                                      std::string* error)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == NULL)
  {
    *error = "database is not initialized";
    return false;
  }

  sqlite3_stmt* statement = NULL;
  const char* sql =
      "SELECT id, conversation_type, conversation_id, sender, receiver, "
      "room_id, content, created_at "
      "FROM messages "
      "ORDER BY id ASC;";
  if (sqlite3_prepare_v2(db_, sql, -1, &statement, NULL) != SQLITE_OK)
  {
    *error = sqlite3_errmsg(db_);
    return false;
  }

  std::ofstream output(outputPath.c_str(), std::ios::out | std::ios::trunc);
  if (!output)
  {
    sqlite3_finalize(statement);
    *error = "cannot open output file";
    return false;
  }

  output << "[\n";
  bool firstMessage = true;
  while (sqlite3_step(statement) == SQLITE_ROW)
  {
    const Message message = messageFromStatement(statement);
    if (!firstMessage)
    {
      output << ",\n";
    }
    firstMessage = false;

    output << "  {";
    bool firstField = true;
    writeJsonIntField(output, "id", message.id, &firstField);
    writeJsonField(output,
                   "conversation_type",
                   message.conversationType,
                   &firstField);
    writeJsonField(output,
                   "conversation_id",
                   message.conversationId,
                   &firstField);
    writeJsonField(output, "sender", message.sender, &firstField);
    writeJsonField(output, "receiver", message.receiver, &firstField);
    writeJsonField(output, "room_id", message.roomId, &firstField);
    writeJsonField(output, "content", message.content, &firstField);
    writeJsonIntField(output, "created_at", message.createdAt, &firstField);
    output << "}";
  }
  output << "\n]\n";

  const int status = sqlite3_finalize(statement);
  if (status != SQLITE_OK)
  {
    *error = sqlite3_errmsg(db_);
    return false;
  }
  if (!output)
  {
    *error = "failed to write output file";
    return false;
  }
  return true;
}

std::string ChatStorage::privateConversationId(const std::string& userA,
                                               const std::string& userB)
{
  if (userA < userB)
  {
    return "private:" + userA + ":" + userB;
  }
  return "private:" + userB + ":" + userA;
}

bool ChatStorage::exec(const char* sql)
{
  char* error = NULL;
  const int status = sqlite3_exec(db_, sql, NULL, NULL, &error);
  if (error != NULL)
  {
    sqlite3_free(error);
  }
  return status == SQLITE_OK;
}

void ChatStorage::close()
{
  if (db_ != NULL)
  {
    sqlite3_close(db_);
    db_ = NULL;
  }
}

}  // namespace chatservice
