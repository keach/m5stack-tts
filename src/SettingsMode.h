#pragma once

#include <M5Stack.h>

#include "AppSettings.h"
#include "SpeechService.h"

struct DiagnosticStatus {
  bool storageAvailable;
  bool dictionaryAvailable;
  bool speechAvailable;
  bool wifiConnected;
  bool timeSynchronized;
  bool weatherAvailable;
};

class SettingsMode {
 public:
  void run(AppSettings& settings, SpeechService& speech,
           bool speechAvailable, const DiagnosticStatus& diagnostics);

 private:
  struct ButtonConfirmation {
    bool pending = false;
    unsigned long detectedAt = 0;
  };

  bool confirmedPress(Button& button, ButtonConfirmation& confirmation);
  void drawMenu(int selectedItem, ClockDisplayPrecision clockPrecision,
                uint8_t volumePercent);
  void showDiagnostics(const DiagnosticStatus& diagnostics);
  void showMessage(const char* title, const char* detail);

  ButtonConfirmation buttonA_;
  ButtonConfirmation buttonB_;
  ButtonConfirmation buttonC_;
};
