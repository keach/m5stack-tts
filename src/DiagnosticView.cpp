#include "DiagnosticView.h"

#include <M5Stack.h>

namespace {
uint16_t stateColor(DiagnosticState state) {
  switch (state) {
    case DiagnosticState::Ok: return TFT_GREEN;
    case DiagnosticState::Wait: return TFT_YELLOW;
    case DiagnosticState::Ng: return TFT_RED;
    case DiagnosticState::Skip: return TFT_ORANGE;
    case DiagnosticState::Pending: return TFT_LIGHTGREY;
  }
  return TFT_RED;
}
}

void drawDiagnosticPage(const DiagnosticModel& model, size_t page, bool startup) {
  page %= DiagnosticModel::PAGE_COUNT;
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_NAVY);
  M5.Lcd.setCursor(12, 8);
  M5.Lcd.printf("%s %u/%u", startup ? "STARTUP CHECK" : "DIAGNOSTICS",
      static_cast<unsigned>(page + 1), static_cast<unsigned>(DiagnosticModel::PAGE_COUNT));
  for (size_t row = 0; row < DiagnosticModel::ROWS_PER_PAGE; ++row) {
    const size_t index = page * DiagnosticModel::ROWS_PER_PAGE + row;
    if (index >= DiagnosticModel::ITEM_COUNT) break;
    const auto item = static_cast<DiagnosticItem>(index);
    const auto& entry = model.get(item);
    const int y = 40 + row * 36;
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(16, y);
    M5.Lcd.print(DiagnosticModel::label(item));
    M5.Lcd.setTextColor(stateColor(entry.state), TFT_BLACK);
    M5.Lcd.setCursor(248, y);
    M5.Lcd.print(diagnosticStateText(entry.state));
    M5.Lcd.setTextSize(1);
    M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Lcd.setCursor(16, y + 19);
    char detail[49];
    snprintf(detail, sizeof(detail), "%s", entry.detail);
    M5.Lcd.print(detail);
  }
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setCursor(16, 184);
  M5.Lcd.printf("IP: %s", model.ip[0] ? model.ip : "-");
  M5.Lcd.setCursor(16, 195);
  M5.Lcd.printf("JST: %s", model.synchronizedTime[0] ? model.synchronizedTime : "-");
  M5.Lcd.setCursor(16, 206);
  M5.Lcd.printf("Web: %s", model.webAvailable ? model.ip : "-");
  M5.Lcd.fillRect(0, 218, 320, 22, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setCursor(30, 225);
  M5.Lcd.print(startup ? "Checking services..." : "A:prev  B:back  C:next");
}
