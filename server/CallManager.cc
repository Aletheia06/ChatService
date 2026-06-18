#include "server/CallManager.h"

namespace chatservice
{

bool CallManager::startCall(const std::string& caller,
                            const std::string& callee,
                            const std::string& callId,
                            std::string* errorReason)
{
  muduo::MutexLockGuard lock(mutex_);
  if (users_.find(caller) != users_.end())
  {
    *errorReason = "caller_busy";
    return false;
  }
  if (users_.find(callee) != users_.end())
  {
    *errorReason = "target_busy";
    return false;
  }
  if (calls_.find(callId) != calls_.end())
  {
    *errorReason = "duplicate_call_id";
    return false;
  }

  CallRecord call;
  call.caller = caller;
  call.callee = callee;
  call.accepted = false;
  calls_[callId] = call;

  UserCall callerState;
  callerState.state = kCallCalling;
  callerState.callId = callId;
  callerState.peer = callee;
  users_[caller] = callerState;

  UserCall calleeState;
  calleeState.state = kCallRinging;
  calleeState.callId = callId;
  calleeState.peer = caller;
  users_[callee] = calleeState;
  return true;
}

bool CallManager::acceptCall(const std::string& callee,
                             const std::string& caller,
                             const std::string& callId,
                             std::string* errorReason)
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, UserCall>::iterator calleeIt =
      users_.find(callee);
  const std::map<std::string, UserCall>::iterator callerIt =
      users_.find(caller);
  const std::map<std::string, CallRecord>::iterator callIt =
      calls_.find(callId);

  if (calleeIt == users_.end() || callerIt == users_.end() ||
      callIt == calls_.end() ||
      calleeIt->second.state != kCallRinging ||
      callerIt->second.state != kCallCalling ||
      calleeIt->second.callId != callId ||
      callerIt->second.callId != callId ||
      calleeIt->second.peer != caller ||
      callerIt->second.peer != callee ||
      callIt->second.caller != caller ||
      callIt->second.callee != callee ||
      callIt->second.accepted)
  {
    *errorReason = "invalid_pending_call";
    return false;
  }

  calleeIt->second.state = kCallInCall;
  callerIt->second.state = kCallInCall;
  callIt->second.accepted = true;
  return true;
}

bool CallManager::rejectCall(const std::string& callee,
                             const std::string& caller,
                             const std::string& callId,
                             std::string* errorReason)
{
  return finishPendingCall(callee,
                           caller,
                           callId,
                           kCallRinging,
                           false,
                           errorReason);
}

bool CallManager::cancelCall(const std::string& caller,
                             const std::string& callee,
                             const std::string& callId,
                             std::string* errorReason)
{
  return finishPendingCall(caller,
                           callee,
                           callId,
                           kCallCalling,
                           true,
                           errorReason);
}

bool CallManager::timeoutCall(const std::string& caller,
                              const std::string& callee,
                              const std::string& callId,
                              std::string* errorReason)
{
  return finishPendingCall(caller,
                           callee,
                           callId,
                           kCallCalling,
                           true,
                           errorReason);
}

bool CallManager::hangupCall(const std::string& username,
                             const std::string& peer,
                             const std::string& callId,
                             std::string* errorReason)
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, UserCall>::const_iterator userIt =
      users_.find(username);
  const std::map<std::string, UserCall>::const_iterator peerIt =
      users_.find(peer);
  const std::map<std::string, CallRecord>::const_iterator callIt =
      calls_.find(callId);

  if (userIt == users_.end() || peerIt == users_.end() ||
      callIt == calls_.end() ||
      userIt->second.state != kCallInCall ||
      peerIt->second.state != kCallInCall ||
      userIt->second.callId != callId ||
      peerIt->second.callId != callId ||
      userIt->second.peer != peer ||
      peerIt->second.peer != username ||
      !callIt->second.accepted)
  {
    *errorReason = "invalid_active_call";
    return false;
  }

  eraseCallLocked(callId);
  return true;
}

bool CallManager::validateSignaling(const std::string& username,
                                    const std::string& peer,
                                    const std::string& callId,
                                    const std::string& type,
                                    std::string* errorReason) const
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, UserCall>::const_iterator userIt =
      users_.find(username);
  const std::map<std::string, UserCall>::const_iterator peerIt =
      users_.find(peer);
  const std::map<std::string, CallRecord>::const_iterator callIt =
      calls_.find(callId);

  if (userIt == users_.end() || peerIt == users_.end() ||
      callIt == calls_.end() ||
      userIt->second.state != kCallInCall ||
      peerIt->second.state != kCallInCall ||
      userIt->second.callId != callId ||
      peerIt->second.callId != callId ||
      userIt->second.peer != peer ||
      peerIt->second.peer != username ||
      !callIt->second.accepted)
  {
    *errorReason = "not_in_same_active_call";
    return false;
  }

  if (type == "webrtc_offer" && callIt->second.caller != username)
  {
    *errorReason = "only_caller_can_offer";
    return false;
  }
  if (type == "webrtc_answer" && callIt->second.callee != username)
  {
    *errorReason = "only_callee_can_answer";
    return false;
  }
  return true;
}

bool CallManager::removeUser(const std::string& username,
                             CallDisconnectEvent* event)
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, UserCall>::const_iterator userIt =
      users_.find(username);
  if (userIt == users_.end())
  {
    return false;
  }

  event->callId = userIt->second.callId;
  event->peer = userIt->second.peer;
  event->disconnectedState = userIt->second.state;
  const std::string callId = userIt->second.callId;
  eraseCallLocked(callId);
  return true;
}

CallUserState CallManager::stateForUser(const std::string& username) const
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, UserCall>::const_iterator it =
      users_.find(username);
  return it == users_.end() ? kCallIdle : it->second.state;
}

bool CallManager::finishPendingCall(const std::string& username,
                                    const std::string& peer,
                                    const std::string& callId,
                                    CallUserState expectedState,
                                    bool expectCaller,
                                    std::string* errorReason)
{
  muduo::MutexLockGuard lock(mutex_);
  const std::map<std::string, UserCall>::const_iterator userIt =
      users_.find(username);
  const std::map<std::string, UserCall>::const_iterator peerIt =
      users_.find(peer);
  const std::map<std::string, CallRecord>::const_iterator callIt =
      calls_.find(callId);

  if (userIt == users_.end() || peerIt == users_.end() ||
      callIt == calls_.end() ||
      userIt->second.state != expectedState ||
      userIt->second.callId != callId ||
      peerIt->second.callId != callId ||
      userIt->second.peer != peer ||
      peerIt->second.peer != username ||
      callIt->second.accepted ||
      (expectCaller && callIt->second.caller != username) ||
      (!expectCaller && callIt->second.callee != username))
  {
    *errorReason = "invalid_pending_call";
    return false;
  }

  eraseCallLocked(callId);
  return true;
}

void CallManager::eraseCallLocked(const std::string& callId)
{
  const std::map<std::string, CallRecord>::const_iterator callIt =
      calls_.find(callId);
  if (callIt == calls_.end())
  {
    return;
  }

  users_.erase(callIt->second.caller);
  users_.erase(callIt->second.callee);
  calls_.erase(callIt);
}

}  // namespace chatservice
