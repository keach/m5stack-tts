#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum class DiagnosticState : uint8_t { Pending, Ok, Wait, Ng, Skip };
enum class DiagnosticItem : uint8_t {
  Storage, Dictionary, Speech, JapaneseFont, Wifi, Ntp, Weather, Forecast,
  WebServer, P2PQuake, Ambient, ThingSpeak, Count
};

inline const char* diagnosticStateText(DiagnosticState state) {
  switch (state) {
    case DiagnosticState::Pending: return "...";
    case DiagnosticState::Ok: return "OK";
    case DiagnosticState::Wait: return "WAIT";
    case DiagnosticState::Ng: return "NG";
    case DiagnosticState::Skip: return "SKIP";
  }
  return "NG";
}

template <typename Result>
DiagnosticState diagnosticPublishState(Result result) {
  switch (result) {
    case Result::Sent: return DiagnosticState::Ok;
    case Result::RequestFailed: return DiagnosticState::Ng;
    case Result::NotAttempted:
    case Result::CredentialsMissing:
    case Result::WiFiDisconnected:
    case Result::TimeUnavailable: return DiagnosticState::Skip;
  }
  return DiagnosticState::Ng;
}

struct DiagnosticEntry {
  DiagnosticState state = DiagnosticState::Pending;
  char detail[64] = {};
};

class DiagnosticModel {
 public:
  static constexpr size_t ITEM_COUNT = static_cast<size_t>(DiagnosticItem::Count);
  static constexpr size_t ROWS_PER_PAGE = 4;
  static constexpr size_t PAGE_COUNT = (ITEM_COUNT + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE;
  static const char* label(DiagnosticItem item) {
    const char* labels[] = {"microSD", "Dictionary", "Speech", "JP font",
        "Wi-Fi", "NTP time", "Weather", "Forecast", "Web server",
        "P2PQuake", "Ambient", "ThingSpeak"};
    return labels[static_cast<size_t>(item)];
  }
  void set(DiagnosticItem item, DiagnosticState state, const char* detail = "") {
    auto& entry = entries_[static_cast<size_t>(item)];
    entry.state = state;
    snprintf(entry.detail, sizeof(entry.detail), "%s", detail);
  }
  const DiagnosticEntry& get(DiagnosticItem item) const {
    return entries_[static_cast<size_t>(item)];
  }
  bool complete() const {
    for (const auto& entry : entries_)
      if (entry.state == DiagnosticState::Pending) return false;
    return true;
  }
  bool sameAs(const DiagnosticModel& other) const {
    if (strcmp(ip, other.ip) || strcmp(synchronizedTime, other.synchronizedTime) ||
        webAvailable != other.webAvailable) return false;
    for (size_t i = 0; i < ITEM_COUNT; ++i)
      if (entries_[i].state != other.entries_[i].state ||
          strcmp(entries_[i].detail, other.entries_[i].detail)) return false;
    return true;
  }
  static size_t pageFor(DiagnosticItem item) {
    return static_cast<size_t>(item) / ROWS_PER_PAGE;
  }
  static size_t adjacentPage(size_t page, bool next) {
    return (page + (next ? 1 : PAGE_COUNT - 1)) % PAGE_COUNT;
  }
  char ip[16] = {};
  char synchronizedTime[24] = {};
  bool webAvailable = false;
 private:
  DiagnosticEntry entries_[ITEM_COUNT];
};

struct DiagnosticSource {
  DiagnosticModel& model;
  void (*refresh)(DiagnosticModel&);
  void update() const { if (refresh) refresh(model); }
};
