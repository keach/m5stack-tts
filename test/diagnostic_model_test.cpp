#include <assert.h>
#include <string.h>
#include "DiagnosticModel.h"

enum class SendResult { NotAttempted, Sent, CredentialsMissing,
                       WiFiDisconnected, TimeUnavailable, RequestFailed };

int main() {
  using S = DiagnosticState;
  DiagnosticModel model;
  assert(!model.complete());
  assert(model.get(DiagnosticItem::Storage).state == S::Pending);
  assert(DiagnosticModel::PAGE_COUNT == 3);
  assert(DiagnosticModel::pageFor(DiagnosticItem::Storage) == 0);
  assert(DiagnosticModel::pageFor(DiagnosticItem::Wifi) == 1);
  assert(DiagnosticModel::pageFor(DiagnosticItem::ThingSpeak) == 2);
  assert(DiagnosticModel::adjacentPage(0, false) == 2);
  assert(DiagnosticModel::adjacentPage(2, true) == 0);
  for (size_t i = 0; i < DiagnosticModel::ITEM_COUNT; ++i)
    model.set(static_cast<DiagnosticItem>(i), S::Skip);
  assert(model.complete());
  assert(strcmp(diagnosticStateText(S::Pending), "...") == 0);
  assert(strcmp(diagnosticStateText(S::Ok), "OK") == 0);
  assert(strcmp(diagnosticStateText(S::Wait), "WAIT") == 0);
  assert(strcmp(diagnosticStateText(S::Ng), "NG") == 0);
  assert(strcmp(diagnosticStateText(S::Skip), "SKIP") == 0);
  assert(diagnosticPublishState(SendResult::Sent) == S::Ok);
  assert(diagnosticPublishState(SendResult::RequestFailed) == S::Ng);
  const SendResult skipped[] = {SendResult::NotAttempted, SendResult::CredentialsMissing,
      SendResult::WiFiDisconnected, SendResult::TimeUnavailable};
  for (auto result : skipped) assert(diagnosticPublishState(result) == S::Skip);
  model.set(DiagnosticItem::Ambient, S::Ok, "Sent; queue: present");
  assert(model.get(DiagnosticItem::Ambient).state == S::Ok);
  DiagnosticModel copy = model;
  assert(model.sameAs(copy));
  copy.set(DiagnosticItem::P2PQuake, S::Wait, "Paused / Production");
  assert(!model.sameAs(copy));
  char longDetail[100];
  memset(longDetail, 'x', sizeof(longDetail) - 1);
  longDetail[sizeof(longDetail) - 1] = '\0';
  model.set(DiagnosticItem::Weather, S::Ng, longDetail);
  assert(strlen(model.get(DiagnosticItem::Weather).detail) == 63);
}
