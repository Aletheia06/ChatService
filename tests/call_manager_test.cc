#include "server/CallManager.h"

#include <string>

namespace
{

bool expectState(chatservice::CallManager* manager,
                 const std::string& username,
                 chatservice::CallUserState expected)
{
  return manager->stateForUser(username) == expected;
}

}  // namespace

int main()
{
  chatservice::CallManager manager;
  std::string error;

  if (!manager.startCall("alice", "bob", "call-ab", &error) ||
      !expectState(&manager, "alice", chatservice::kCallCalling) ||
      !expectState(&manager, "bob", chatservice::kCallRinging))
  {
    return 1;
  }

  if (manager.startCall("charlie", "alice", "call-ca", &error) ||
      error != "target_busy")
  {
    return 2;
  }

  if (!manager.startCall("charlie", "david", "call-cd", &error) ||
      !manager.acceptCall("bob", "alice", "call-ab", &error) ||
      !manager.acceptCall("david", "charlie", "call-cd", &error))
  {
    return 3;
  }

  if (!manager.validateSignaling("alice",
                                 "bob",
                                 "call-ab",
                                 "webrtc_offer",
                                 &error) ||
      manager.validateSignaling("bob",
                                "alice",
                                "call-ab",
                                "webrtc_offer",
                                &error) ||
      error != "only_caller_can_offer")
  {
    return 4;
  }

  if (!manager.hangupCall("bob", "alice", "call-ab", &error) ||
      !expectState(&manager, "alice", chatservice::kCallIdle) ||
      !expectState(&manager, "bob", chatservice::kCallIdle) ||
      !expectState(&manager, "charlie", chatservice::kCallInCall) ||
      !expectState(&manager, "david", chatservice::kCallInCall))
  {
    return 5;
  }

  chatservice::CallDisconnectEvent event;
  if (!manager.removeUser("charlie", &event) ||
      event.callId != "call-cd" ||
      event.peer != "david" ||
      event.disconnectedState != chatservice::kCallInCall ||
      !expectState(&manager, "david", chatservice::kCallIdle))
  {
    return 6;
  }

  if (!manager.startCall("eve", "frank", "call-ef", &error) ||
      !manager.rejectCall("frank", "eve", "call-ef", &error) ||
      !expectState(&manager, "eve", chatservice::kCallIdle) ||
      !expectState(&manager, "frank", chatservice::kCallIdle))
  {
    return 7;
  }

  if (!manager.startCall("george", "harry", "call-gh", &error) ||
      !manager.cancelCall("george", "harry", "call-gh", &error) ||
      !manager.startCall("ivy", "jane", "call-ij", &error) ||
      !manager.timeoutCall("ivy", "jane", "call-ij", &error) ||
      !expectState(&manager, "george", chatservice::kCallIdle) ||
      !expectState(&manager, "harry", chatservice::kCallIdle) ||
      !expectState(&manager, "ivy", chatservice::kCallIdle) ||
      !expectState(&manager, "jane", chatservice::kCallIdle))
  {
    return 8;
  }

  return 0;
}
