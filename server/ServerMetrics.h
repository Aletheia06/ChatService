#ifndef CHATSERVICE_SERVER_SERVERMETRICS_H
#define CHATSERVICE_SERVER_SERVERMETRICS_H

#include "muduo/base/Atomic.h"

#include <stdint.h>

namespace chatservice
{

struct MetricsSnapshot
{
  int64_t onlineUsers;
  int64_t activeRooms;
  int64_t intervalMessages;
  int64_t totalMessages;
  int64_t totalConnections;
  int64_t droppedConnections;
  double messagesPerSecond;
  double averageLatencyMicros;
};

class ServerMetrics
{
 public:
  ServerMetrics();

  void recordConnectionOpened();
  void recordConnectionDropped();
  void recordMessage(int64_t latencyMicros);

  MetricsSnapshot snapshotAndReset(int64_t onlineUsers,
                                   int64_t activeRooms,
                                   double intervalSeconds);

 private:
  muduo::AtomicInt64 totalConnections_;
  muduo::AtomicInt64 droppedConnections_;
  muduo::AtomicInt64 totalMessages_;
  muduo::AtomicInt64 intervalMessages_;
  muduo::AtomicInt64 intervalLatencyMicros_;
  muduo::AtomicInt64 intervalLatencySamples_;
};

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_SERVERMETRICS_H
