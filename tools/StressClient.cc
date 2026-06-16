#include "common/Config.h"
#include "common/Json.h"
#include "common/Protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

struct Options
{
  std::string host;
  uint16_t port;
  int clients;
  int durationSeconds;
  std::string csvPath;

  Options()
    : host(chatservice::kServerHost),
      port(chatservice::kServerPort),
      clients(1000),
      durationSeconds(60),
      csvPath("stress_results.csv")
  {
  }
};

struct Metrics
{
  std::atomic<long long> connectionAttempts;
  std::atomic<long long> connectionSuccesses;
  std::atomic<long long> connectionFailures;
  std::atomic<long long> messagesSent;
  std::atomic<long long> messagesReceived;
  std::atomic<long long> latencyMicros;
  std::atomic<long long> latencySamples;

  Metrics()
    : connectionAttempts(0),
      connectionSuccesses(0),
      connectionFailures(0),
      messagesSent(0),
      messagesReceived(0),
      latencyMicros(0),
      latencySamples(0)
  {
  }
};

struct StartGate
{
  std::mutex mutex;
  std::condition_variable condition;
  bool started;

  StartGate()
    : started(false)
  {
  }
};

void printUsage(const char* program)
{
  std::cout << "Usage: " << program
            << " [--clients N] [--duration SECONDS] [--host HOST]"
            << " [--port PORT] [--csv PATH]\n";
}

bool parseInt(const char* text, int* value)
{
  char* end = NULL;
  const long parsed = strtol(text, &end, 10);
  if (end == text || *end != '\0' || parsed < 0 ||
      parsed > 2147483647L)
  {
    return false;
  }
  *value = static_cast<int>(parsed);
  return true;
}

bool parseOptions(int argc, char* argv[], Options* options)
{
  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if (arg == "--help")
    {
      printUsage(argv[0]);
      return false;
    }
    if (i + 1 >= argc)
    {
      std::cerr << "Missing value for " << arg << "\n";
      return false;
    }

    if (arg == "--clients")
    {
      if (!parseInt(argv[++i], &options->clients) || options->clients <= 0)
      {
        std::cerr << "Invalid client count\n";
        return false;
      }
    }
    else if (arg == "--duration")
    {
      if (!parseInt(argv[++i], &options->durationSeconds) ||
          options->durationSeconds <= 0)
      {
        std::cerr << "Invalid duration\n";
        return false;
      }
    }
    else if (arg == "--host")
    {
      options->host = argv[++i];
    }
    else if (arg == "--port")
    {
      int port = 0;
      if (!parseInt(argv[++i], &port) || port <= 0 || port > 65535)
      {
        std::cerr << "Invalid port\n";
        return false;
      }
      options->port = static_cast<uint16_t>(port);
    }
    else if (arg == "--csv")
    {
      options->csvPath = argv[++i];
    }
    else
    {
      std::cerr << "Unknown argument: " << arg << "\n";
      return false;
    }
  }

  return true;
}

void raiseFileLimit(int clients)
{
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0)
  {
    return;
  }

  const rlim_t target = static_cast<rlim_t>(clients + 128);
  if (limit.rlim_cur >= target)
  {
    return;
  }

  struct rlimit updated = limit;
  updated.rlim_cur = target;
  if (updated.rlim_cur > updated.rlim_max)
  {
    updated.rlim_cur = updated.rlim_max;
  }

  if (setrlimit(RLIMIT_NOFILE, &updated) != 0 ||
      updated.rlim_cur < target)
  {
    std::cerr << "Warning: file descriptor limit may be too low for "
              << clients << " clients\n";
  }
}

int connectToServer(const std::string& host, uint16_t port)
{
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  std::ostringstream portStream;
  portStream << port;

  struct addrinfo* result = NULL;
  if (getaddrinfo(host.c_str(), portStream.str().c_str(), &hints, &result) != 0)
  {
    return -1;
  }

  int sockfd = -1;
  for (struct addrinfo* it = result; it != NULL; it = it->ai_next)
  {
    sockfd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (sockfd < 0)
    {
      continue;
    }

    const int one = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);

    if (connect(sockfd, it->ai_addr, it->ai_addrlen) == 0)
    {
      break;
    }

    close(sockfd);
    sockfd = -1;
  }

  freeaddrinfo(result);
  return sockfd;
}

bool sendAll(int sockfd, const std::string& data)
{
  const char* cursor = data.data();
  std::size_t remaining = data.size();
  while (remaining > 0)
  {
    const ssize_t written = send(sockfd, cursor, remaining, MSG_NOSIGNAL);
    if (written < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      return false;
    }
    if (written == 0)
    {
      return false;
    }

    cursor += written;
    remaining -= static_cast<std::size_t>(written);
  }
  return true;
}

std::string makeLoginLine(const std::string& username)
{
  chatservice::JsonObject object;
  object["type"] = "login";
  object["username"] = username;
  object["sent_at_us"] = std::to_string(chatservice::currentTimeMicros());
  return chatservice::makeJsonLine(object);
}

std::string makePrivateLine(const std::string& username, long long sequence)
{
  const int64_t sentAt = chatservice::currentTimeMicros();
  std::ostringstream payload;
  payload << "client=" << username
          << ";seq=" << sequence
          << ";sent_at_us=" << sentAt;

  chatservice::JsonObject object;
  object["type"] = "private";
  object["target"] = username;
  object["message"] = payload.str();
  object["sent_at_us"] = std::to_string(sentAt);
  return chatservice::makeJsonLine(object);
}

bool parseSentAt(const std::string& message, int64_t* sentAt)
{
  const std::string key = "sent_at_us=";
  const std::size_t start = message.find(key);
  if (start == std::string::npos)
  {
    return false;
  }

  const std::size_t valueStart = start + key.size();
  const std::size_t end = message.find(';', valueStart);
  const std::string value = message.substr(
      valueStart,
      end == std::string::npos ? std::string::npos : end - valueStart);

  char* parseEnd = NULL;
  const long long parsed = strtoll(value.c_str(), &parseEnd, 10);
  if (parseEnd == value.c_str() || *parseEnd != '\0' || parsed <= 0)
  {
    return false;
  }

  *sentAt = parsed;
  return true;
}

void handleLine(const std::string& line, Metrics* metrics)
{
  chatservice::JsonObject object;
  std::string error;
  if (!chatservice::parseJsonObject(line, &object, &error))
  {
    return;
  }

  if (object["type"] != "private")
  {
    return;
  }

  int64_t sentAt = 0;
  if (!parseSentAt(object["message"], &sentAt))
  {
    return;
  }

  const int64_t now = chatservice::currentTimeMicros();
  if (now >= sentAt)
  {
    metrics->latencyMicros.fetch_add(now - sentAt);
    metrics->latencySamples.fetch_add(1);
  }
  metrics->messagesReceived.fetch_add(1);
}

void drainSocket(int sockfd, std::string* buffer, Metrics* metrics)
{
  char temp[4096];
  while (true)
  {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(sockfd, &readSet);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    const int ready = select(sockfd + 1, &readSet, NULL, NULL, &timeout);
    if (ready <= 0)
    {
      return;
    }

    const ssize_t received = recv(sockfd, temp, sizeof temp, 0);
    if (received <= 0)
    {
      return;
    }

    buffer->append(temp, static_cast<std::size_t>(received));
    while (true)
    {
      const std::size_t eol = buffer->find('\n');
      if (eol == std::string::npos)
      {
        break;
      }

      std::string line = buffer->substr(0, eol);
      if (!line.empty() && line[line.size() - 1] == '\r')
      {
        line.resize(line.size() - 1);
      }
      buffer->erase(0, eol + 1);
      handleLine(line, metrics);
    }
  }
}

void clientThread(int clientId,
                  const Options& options,
                  const std::chrono::steady_clock::time_point& endTime,
                  StartGate* gate,
                  Metrics* metrics)
{
  {
    std::unique_lock<std::mutex> lock(gate->mutex);
    while (!gate->started)
    {
      gate->condition.wait(lock);
    }
  }

  metrics->connectionAttempts.fetch_add(1);
  const int sockfd = connectToServer(options.host, options.port);
  if (sockfd < 0)
  {
    metrics->connectionFailures.fetch_add(1);
    return;
  }

  metrics->connectionSuccesses.fetch_add(1);

  std::ostringstream userStream;
  userStream << "stress_" << clientId;
  const std::string username = userStream.str();

  if (!sendAll(sockfd, makeLoginLine(username)))
  {
    close(sockfd);
    return;
  }

  long long sequence = 0;
  std::string receiveBuffer;
  std::chrono::steady_clock::time_point nextSend =
      std::chrono::steady_clock::now();

  while (std::chrono::steady_clock::now() < endTime)
  {
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    if (now >= nextSend)
    {
      if (!sendAll(sockfd, makePrivateLine(username, ++sequence)))
      {
        break;
      }
      metrics->messagesSent.fetch_add(1);
      nextSend += std::chrono::seconds(1);
    }

    drainSocket(sockfd, &receiveBuffer, metrics);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  drainSocket(sockfd, &receiveBuffer, metrics);
  close(sockfd);
}

bool writeCsv(const Options& options,
              long long elapsedMicros,
              const Metrics& metrics)
{
  const long long attempts = metrics.connectionAttempts.load();
  const long long successes = metrics.connectionSuccesses.load();
  const long long failures = metrics.connectionFailures.load();
  const long long sent = metrics.messagesSent.load();
  const long long received = metrics.messagesReceived.load();
  const long long latencySamples = metrics.latencySamples.load();
  const long long latencyTotal = metrics.latencyMicros.load();

  const double elapsedSeconds =
      static_cast<double>(elapsedMicros) / 1000000.0;
  const double successRate =
      attempts == 0 ? 0.0 : static_cast<double>(successes) * 100.0 /
                             static_cast<double>(attempts);
  const double averageLatency =
      latencySamples == 0 ? 0.0 : static_cast<double>(latencyTotal) /
                               static_cast<double>(latencySamples);
  const double throughput =
      elapsedSeconds <= 0.0 ? 0.0 : static_cast<double>(received) /
                                  elapsedSeconds;

  std::ofstream output(options.csvPath.c_str(), std::ios::out | std::ios::trunc);
  if (!output)
  {
    return false;
  }

  output << "clients,duration_seconds,elapsed_seconds,"
         << "connection_attempts,successful_connections,failed_connections,"
         << "connection_success_rate,messages_sent,messages_received,"
         << "average_latency_us,throughput_messages_per_second\n";
  output << options.clients << ','
         << options.durationSeconds << ','
         << elapsedSeconds << ','
         << attempts << ','
         << successes << ','
         << failures << ','
         << successRate << ','
         << sent << ','
         << received << ','
         << averageLatency << ','
         << throughput << '\n';
  return true;
}

}  // namespace

int main(int argc, char* argv[])
{
  Options options;
  if (!parseOptions(argc, argv, &options))
  {
    return 1;
  }

  raiseFileLimit(options.clients);

  Metrics metrics;
  StartGate gate;
  std::vector<std::thread> threads;
  threads.reserve(static_cast<std::size_t>(options.clients));

  const std::chrono::steady_clock::time_point startTime =
      std::chrono::steady_clock::now();
  const std::chrono::steady_clock::time_point endTime =
      startTime + std::chrono::seconds(options.durationSeconds);

  for (int i = 0; i < options.clients; ++i)
  {
    threads.push_back(std::thread(clientThread,
                                  i,
                                  std::cref(options),
                                  std::cref(endTime),
                                  &gate,
                                  &metrics));
  }

  {
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.started = true;
  }
  gate.condition.notify_all();

  for (std::vector<std::thread>::iterator it = threads.begin();
       it != threads.end();
       ++it)
  {
    it->join();
  }

  const std::chrono::steady_clock::time_point finishTime =
      std::chrono::steady_clock::now();
  const long long elapsedMicros =
      std::chrono::duration_cast<std::chrono::microseconds>(
          finishTime - startTime).count();

  if (!writeCsv(options, elapsedMicros, metrics))
  {
    std::cerr << "Failed to write CSV: " << options.csvPath << "\n";
    return 1;
  }

  const long long attempts = metrics.connectionAttempts.load();
  const long long successes = metrics.connectionSuccesses.load();
  const long long received = metrics.messagesReceived.load();
  const double successRate =
      attempts == 0 ? 0.0 : static_cast<double>(successes) * 100.0 /
                             static_cast<double>(attempts);
  const double throughput =
      elapsedMicros <= 0 ? 0.0 : static_cast<double>(received) * 1000000.0 /
                               static_cast<double>(elapsedMicros);

  std::cout << "clients=" << options.clients
            << " successful_connections=" << successes
            << " connection_success_rate=" << successRate
            << " messages_sent=" << metrics.messagesSent.load()
            << " messages_received=" << received
            << " throughput_messages_per_second=" << throughput
            << " csv=" << options.csvPath << "\n";

  return 0;
}
