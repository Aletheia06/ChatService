#include "server/ServerMetrics.h"

namespace chatservice
{

ServerMetrics::ServerMetrics()
  : totalConnections_(),
    droppedConnections_(),
    totalMessages_(),
    intervalMessages_(),
    intervalLatencyMicros_(),
    intervalLatencySamples_()
{
}

void ServerMetrics::recordConnectionOpened()
{
  totalConnections_.increment();
}

void ServerMetrics::recordConnectionDropped()
{
  droppedConnections_.increment();
}

void ServerMetrics::recordMessage(int64_t latencyMicros)
{
  if (latencyMicros < 0)
  {
    latencyMicros = 0;
  }

  totalMessages_.increment();
  intervalMessages_.increment();
  intervalLatencyMicros_.add(latencyMicros);
  intervalLatencySamples_.increment();
}

MetricsSnapshot ServerMetrics::snapshotAndReset(int64_t onlineUsers,
                                                int64_t activeRooms,
                                                double intervalSeconds)
{
  MetricsSnapshot snapshot;
  snapshot.onlineUsers = onlineUsers;
  snapshot.activeRooms = activeRooms;
  snapshot.intervalMessages = intervalMessages_.getAndSet(0);
  snapshot.totalMessages = totalMessages_.get();
  snapshot.totalConnections = totalConnections_.get();
  snapshot.droppedConnections = droppedConnections_.get();

  if (intervalSeconds <= 0.0)
  {
    snapshot.messagesPerSecond = 0.0;
  }
  else
  {
    snapshot.messagesPerSecond =
        static_cast<double>(snapshot.intervalMessages) / intervalSeconds;
  }

  const int64_t latencyMicros = intervalLatencyMicros_.getAndSet(0);
  const int64_t latencySamples = intervalLatencySamples_.getAndSet(0);
  if (latencySamples == 0)
  {
    snapshot.averageLatencyMicros = 0.0;
  }
  else
  {
    snapshot.averageLatencyMicros =
        static_cast<double>(latencyMicros) /
        static_cast<double>(latencySamples);
  }

  return snapshot;
}

}  // namespace chatservice
