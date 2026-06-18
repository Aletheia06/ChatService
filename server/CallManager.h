#ifndef CHATSERVICE_SERVER_CALLMANAGER_H
#define CHATSERVICE_SERVER_CALLMANAGER_H

#include "muduo/base/Mutex.h"

#include <map>
#include <string>

namespace chatservice
{

enum CallUserState
{
  kCallIdle,
  kCallCalling,
  kCallRinging,
  kCallInCall
};

struct CallDisconnectEvent
{
  std::string callId;
  std::string peer;
  CallUserState disconnectedState;
};

class CallManager
{
 public:
  bool startCall(const std::string& caller,
                 const std::string& callee,
                 const std::string& callId,
                 std::string* errorReason);
  bool acceptCall(const std::string& callee,
                  const std::string& caller,
                  const std::string& callId,
                  std::string* errorReason);
  bool rejectCall(const std::string& callee,
                  const std::string& caller,
                  const std::string& callId,
                  std::string* errorReason);
  bool cancelCall(const std::string& caller,
                  const std::string& callee,
                  const std::string& callId,
                  std::string* errorReason);
  bool timeoutCall(const std::string& caller,
                   const std::string& callee,
                   const std::string& callId,
                   std::string* errorReason);
  bool hangupCall(const std::string& username,
                  const std::string& peer,
                  const std::string& callId,
                  std::string* errorReason);
  bool validateSignaling(const std::string& username,
                         const std::string& peer,
                         const std::string& callId,
                         const std::string& type,
                         std::string* errorReason) const;
  bool removeUser(const std::string& username, CallDisconnectEvent* event);
  CallUserState stateForUser(const std::string& username) const;

 private:
  struct UserCall
  {
    CallUserState state;
    std::string callId;
    std::string peer;
  };

  struct CallRecord
  {
    std::string caller;
    std::string callee;
    bool accepted;
  };

  bool finishPendingCall(const std::string& username,
                         const std::string& peer,
                         const std::string& callId,
                         CallUserState expectedState,
                         bool expectCaller,
                         std::string* errorReason);
  void eraseCallLocked(const std::string& callId);

  mutable muduo::MutexLock mutex_;
  std::map<std::string, UserCall> users_ GUARDED_BY(mutex_);
  std::map<std::string, CallRecord> calls_ GUARDED_BY(mutex_);
};

}  // namespace chatservice

#endif  // CHATSERVICE_SERVER_CALLMANAGER_H
