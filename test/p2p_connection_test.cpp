#include <assert.h>
#include <string.h>
#include "P2PConnectionStatus.h"

int main() {
  using S = P2PConnectionState;
  assert(resolveP2PConnectionState(false, false, true, false, false, false) == S::NotStarted);
  assert(resolveP2PConnectionState(true, false, false, false, false, false) == S::WaitingWifi);
  assert(resolveP2PConnectionState(true, false, true, false, false, false) == S::Waiting);
  assert(resolveP2PConnectionState(true, false, true, false, true, false) == S::Connecting);
  assert(resolveP2PConnectionState(true, false, true, true, false, false) == S::Connected);
  assert(resolveP2PConnectionState(true, true, false, false, false, true) == S::Paused);
  assert(resolveP2PConnectionState(true, false, true, false, true, true) == S::Error);
  assert(strcmp(p2pConnectionSummary(S::Connected), "OK") == 0);
  assert(strcmp(p2pConnectionSummary(S::NotStarted), "NG") == 0);
  const S waiting[] = {S::WaitingWifi, S::Waiting, S::Connecting, S::Paused, S::Error};
  for (S state : waiting) assert(strcmp(p2pConnectionSummary(state), "WAIT") == 0);
  assert(strcmp(p2pConnectionStateText(S::Paused), "Paused") == 0);
  assert(strcmp(p2pConnectionStateText(S::WaitingWifi), "Waiting Wi-Fi") == 0);
}
