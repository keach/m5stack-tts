#pragma once

#include <stdint.h>

enum class P2PConnectionState : uint8_t {
  NotStarted, WaitingWifi, Waiting, Connecting, Connected, Paused, Error
};

inline P2PConnectionState resolveP2PConnectionState(
    bool started, bool paused, bool wifi, bool connected, bool connecting,
    bool error) {
  if (!started) return P2PConnectionState::NotStarted;
  if (paused) return P2PConnectionState::Paused;
  if (!wifi) return P2PConnectionState::WaitingWifi;
  if (error) return P2PConnectionState::Error;
  if (connected) return P2PConnectionState::Connected;
  if (connecting) return P2PConnectionState::Connecting;
  return P2PConnectionState::Waiting;
}

inline const char* p2pConnectionStateText(P2PConnectionState state) {
  switch (state) {
    case P2PConnectionState::NotStarted: return "Not started";
    case P2PConnectionState::WaitingWifi: return "Waiting Wi-Fi";
    case P2PConnectionState::Waiting: return "Waiting";
    case P2PConnectionState::Connecting: return "Connecting";
    case P2PConnectionState::Connected: return "Connected";
    case P2PConnectionState::Paused: return "Paused";
    case P2PConnectionState::Error: return "Error";
  }
  return "Error";
}

inline const char* p2pConnectionSummary(P2PConnectionState state) {
  if (state == P2PConnectionState::Connected) return "OK";
  if (state == P2PConnectionState::NotStarted) return "NG";
  return "WAIT";
}
