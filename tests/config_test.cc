#include "common/Config.h"

#include <string>

int main()
{
  if (std::string(chatservice::kServerHost) != "127.0.0.1")
  {
    return 1;
  }

  if (chatservice::kServerPort != 8888)
  {
    return 1;
  }

  return 0;
}
