#ifndef CHATSERVICE_COMMON_PROTOCOL_H
#define CHATSERVICE_COMMON_PROTOCOL_H

#include "common/Json.h"

#include <string>
#include <vector>

namespace chatservice
{

bool isValidName(const std::string& name);
int64_t currentTimeMicros();
std::string trimCopy(const std::string& value);
std::string joinList(const std::vector<std::string>& values);

JsonObject makeOkResponse(const std::string& message);
JsonObject makeErrorResponse(const std::string& message);

bool buildRequestFromCommand(const std::string& line,
                             std::string* jsonLine,
                             std::string* error);

}  // namespace chatservice

#endif  // CHATSERVICE_COMMON_PROTOCOL_H
