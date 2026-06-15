#include "common/Json.h"
#include "common/Protocol.h"

#include <string>

namespace
{

bool parseLine(const std::string& line, chatservice::JsonObject* object)
{
  std::string error;
  return chatservice::parseJsonObject(chatservice::trimCopy(line),
                                      object,
                                      &error);
}

}  // namespace

int main()
{
  chatservice::JsonObject object;
  std::string error;

  if (!chatservice::parseJsonObject(
          "{\"type\":\"private\",\"target\":\"alice\",\"message\":\"hello\"}",
          &object,
          &error))
  {
    return 1;
  }
  if (object["type"] != "private" ||
      object["target"] != "alice" ||
      object["message"] != "hello")
  {
    return 1;
  }

  const std::string serialized = chatservice::serializeJsonObject(object);
  chatservice::JsonObject roundTrip;
  if (!chatservice::parseJsonObject(serialized, &roundTrip, &error))
  {
    return 1;
  }
  if (roundTrip["message"] != "hello")
  {
    return 1;
  }

  std::string line;
  if (!chatservice::buildRequestFromCommand("LOGIN alice", &line, &error) ||
      !parseLine(line, &object) ||
      object["type"] != "login" ||
      object["username"] != "alice")
  {
    return 1;
  }

  if (!chatservice::buildRequestFromCommand(
          "PRIVATE bob hello there", &line, &error) ||
      !parseLine(line, &object) ||
      object["target"] != "bob" ||
      object["message"] != "hello there")
  {
    return 1;
  }

  if (!chatservice::buildRequestFromCommand(
          "ROOM_MSG lobby hello room", &line, &error) ||
      !parseLine(line, &object) ||
      object["type"] != "room_msg" ||
      object["room"] != "lobby" ||
      object["message"] != "hello room")
  {
    return 1;
  }

  if (chatservice::buildRequestFromCommand("PRIVATE bob", &line, &error))
  {
    return 1;
  }

  if (!chatservice::isValidName("room-1") ||
      chatservice::isValidName("bad room"))
  {
    return 1;
  }

  return 0;
}
