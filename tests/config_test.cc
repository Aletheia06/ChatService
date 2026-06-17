#include "common/Config.h"

#include <string>

int main()
{
  if (std::string(chatservice::kServerHost).empty())
  {
    return 1;
  }

  if (chatservice::kServerPort != 8888)
  {
    return 1;
  }

  if (chatservice::kWorkerThreadCount < 1)
  {
    return 1;
  }

  if (chatservice::kMetricsIntervalSeconds <= 0.0)
  {
    return 1;
  }

  return 0;
}
