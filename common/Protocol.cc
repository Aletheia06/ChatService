#include "common/Protocol.h"

#include <chrono>
#include <cctype>
#include <sstream>

namespace chatservice
{
namespace
{

std::string toUpperCopy(const std::string& value)
{
  std::string result = value;
  for (std::size_t i = 0; i < result.size(); ++i)
  {
    result[i] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(result[i])));
  }
  return result;
}

void skipSpaces(const std::string& value, std::size_t* pos)
{
  while (*pos < value.size() &&
         std::isspace(static_cast<unsigned char>(value[*pos])) != 0)
  {
    ++(*pos);
  }
}

bool readToken(const std::string& value,
               std::size_t* pos,
               std::string* token)
{
  skipSpaces(value, pos);
  if (*pos >= value.size())
  {
    return false;
  }

  const std::size_t start = *pos;
  while (*pos < value.size() &&
         std::isspace(static_cast<unsigned char>(value[*pos])) == 0)
  {
    ++(*pos);
  }
  *token = value.substr(start, *pos - start);
  return true;
}

bool hasExtraText(const std::string& value, std::size_t pos)
{
  skipSpaces(value, &pos);
  return pos < value.size();
}

std::string restOfLine(const std::string& value, std::size_t pos)
{
  if (pos >= value.size())
  {
    return std::string();
  }
  return trimCopy(value.substr(pos));
}

bool finishObject(const JsonObject& object, std::string* jsonLine)
{
  JsonObject timedObject = object;
  std::ostringstream stream;
  stream << currentTimeMicros();
  timedObject["sent_at_us"] = stream.str();
  *jsonLine = makeJsonLine(timedObject);
  return true;
}

}  // namespace

bool isValidName(const std::string& name)
{
  if (name.empty() || name.size() > 32)
  {
    return false;
  }

  for (std::string::const_iterator it = name.begin(); it != name.end(); ++it)
  {
    const unsigned char ch = static_cast<unsigned char>(*it);
    if (std::isalnum(ch) == 0 && *it != '_' && *it != '-' && *it != '.')
    {
      return false;
    }
  }

  return true;
}

int64_t currentTimeMicros()
{
  const std::chrono::system_clock::duration now =
      std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

std::string trimCopy(const std::string& value)
{
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos)
  {
    return std::string();
  }

  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

std::string joinList(const std::vector<std::string>& values)
{
  std::ostringstream stream;
  for (std::size_t i = 0; i < values.size(); ++i)
  {
    if (i != 0)
    {
      stream << ",";
    }
    stream << values[i];
  }
  return stream.str();
}

JsonObject makeOkResponse(const std::string& message)
{
  JsonObject object;
  object["type"] = "ok";
  object["message"] = message;
  return object;
}

JsonObject makeErrorResponse(const std::string& message)
{
  JsonObject object;
  object["type"] = "error";
  object["message"] = message;
  return object;
}

bool buildRequestFromCommand(const std::string& line,
                             std::string* jsonLine,
                             std::string* error)
{
  const std::string trimmed = trimCopy(line);
  if (trimmed.empty())
  {
    *error = "empty command";
    return false;
  }

  if (trimmed[0] == '{')
  {
    JsonObject parsed;
    if (!parseJsonObject(trimmed, &parsed, error))
    {
      return false;
    }
    *jsonLine = trimmed + "\n";
    return true;
  }

  std::size_t pos = 0;
  std::string command;
  if (!readToken(trimmed, &pos, &command))
  {
    *error = "empty command";
    return false;
  }

  command = toUpperCopy(command);
  JsonObject object;

  if (command == "LOGIN")
  {
    std::string username;
    if (!readToken(trimmed, &pos, &username) || hasExtraText(trimmed, pos))
    {
      *error = "usage: LOGIN username";
      return false;
    }
    object["type"] = "login";
    object["username"] = username;
    return finishObject(object, jsonLine);
  }

  if (command == "LOGOUT")
  {
    if (hasExtraText(trimmed, pos))
    {
      *error = "usage: LOGOUT";
      return false;
    }
    object["type"] = "logout";
    return finishObject(object, jsonLine);
  }

  if (command == "USERS")
  {
    if (hasExtraText(trimmed, pos))
    {
      *error = "usage: USERS";
      return false;
    }
    object["type"] = "users";
    return finishObject(object, jsonLine);
  }

  if (command == "PRIVATE")
  {
    std::string target;
    if (!readToken(trimmed, &pos, &target))
    {
      *error = "usage: PRIVATE target_username message";
      return false;
    }

    const std::string message = restOfLine(trimmed, pos);
    if (message.empty())
    {
      *error = "usage: PRIVATE target_username message";
      return false;
    }

    object["type"] = "private";
    object["target"] = target;
    object["message"] = message;
    return finishObject(object, jsonLine);
  }

  if (command == "CREATE_ROOM")
  {
    std::string room;
    if (!readToken(trimmed, &pos, &room) || hasExtraText(trimmed, pos))
    {
      *error = "usage: CREATE_ROOM room_name";
      return false;
    }
    object["type"] = "create_room";
    object["room"] = room;
    return finishObject(object, jsonLine);
  }

  if (command == "JOIN")
  {
    std::string room;
    if (!readToken(trimmed, &pos, &room) || hasExtraText(trimmed, pos))
    {
      *error = "usage: JOIN room_name";
      return false;
    }
    object["type"] = "join";
    object["room"] = room;
    return finishObject(object, jsonLine);
  }

  if (command == "LEAVE")
  {
    std::string room;
    if (!readToken(trimmed, &pos, &room) || hasExtraText(trimmed, pos))
    {
      *error = "usage: LEAVE room_name";
      return false;
    }
    object["type"] = "leave";
    object["room"] = room;
    return finishObject(object, jsonLine);
  }

  if (command == "ROOM_MSG")
  {
    std::string room;
    if (!readToken(trimmed, &pos, &room))
    {
      *error = "usage: ROOM_MSG room_name message";
      return false;
    }

    const std::string message = restOfLine(trimmed, pos);
    if (message.empty())
    {
      *error = "usage: ROOM_MSG room_name message";
      return false;
    }

    object["type"] = "room_msg";
    object["room"] = room;
    object["message"] = message;
    return finishObject(object, jsonLine);
  }

  if (command == "HISTORY_PRIVATE")
  {
    std::string peer;
    if (!readToken(trimmed, &pos, &peer))
    {
      *error = "usage: HISTORY_PRIVATE peer [limit]";
      return false;
    }

    object["type"] = "history_private";
    object["peer"] = peer;
    std::string limit;
    if (readToken(trimmed, &pos, &limit))
    {
      object["limit"] = limit;
    }
    if (hasExtraText(trimmed, pos))
    {
      *error = "usage: HISTORY_PRIVATE peer [limit]";
      return false;
    }
    return finishObject(object, jsonLine);
  }

  if (command == "HISTORY_ROOM")
  {
    std::string room;
    if (!readToken(trimmed, &pos, &room))
    {
      *error = "usage: HISTORY_ROOM room [limit]";
      return false;
    }

    object["type"] = "history_room";
    object["room"] = room;
    std::string limit;
    if (readToken(trimmed, &pos, &limit))
    {
      object["limit"] = limit;
    }
    if (hasExtraText(trimmed, pos))
    {
      *error = "usage: HISTORY_ROOM room [limit]";
      return false;
    }
    return finishObject(object, jsonLine);
  }

  *error = "unknown command";
  return false;
}

}  // namespace chatservice
