#ifndef CHATSERVICE_COMMON_JSON_H
#define CHATSERVICE_COMMON_JSON_H

#include <map>
#include <string>

namespace chatservice
{

typedef std::map<std::string, std::string> JsonObject;

bool parseJsonObject(const std::string& text,
                     JsonObject* object,
                     std::string* error);
std::string serializeJsonObject(const JsonObject& object);
std::string makeJsonLine(const JsonObject& object);
std::string escapeJsonString(const std::string& value);

}  // namespace chatservice

#endif  // CHATSERVICE_COMMON_JSON_H
