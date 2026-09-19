#pragma once

#include <M5Stack.h>
#include <WiFi.h>

#include "AppSettings.h"
#include "SpeechService.h"
#include "DiagnosticModel.h"

using DiagnosticStatus = DiagnosticSource;

class SettingsMode {
 public:
  void run(AppSettings& settings, SpeechService& speech,
           bool speechAvailable, const DiagnosticStatus& diagnostics);

 private:
  enum class DisplaySleepUpdate {
    Awake,
    Sleeping,
    Woke,
  };

  struct ButtonConfirmation {
    bool pending = false;
    unsigned long detectedAt = 0;
  };

  bool confirmedPress(Button& button, ButtonConfirmation& confirmation);
  void drawMenu(int selectedItem, ClockDisplayPrecision clockPrecision,
                uint8_t volumePercent, bool displaySleepEnabled,
                uint8_t displaySleepMinutes,
                uint8_t displayBrightnessPercent,
                bool eewSpeechEnabled, bool earthquakeSpeechEnabled,
                const AppSettings::ForecastSchedule* forecastSchedules);
  void showDiagnostics(const DiagnosticStatus& diagnostics,
                       bool displaySleepEnabled, uint8_t displaySleepMinutes,
                       uint8_t displayBrightnessPercent);
  void drawDiagnostics(const DiagnosticStatus& diagnostics, size_t page);
  void showFirmwareInfo(bool displaySleepEnabled,
                        uint8_t displaySleepMinutes,
                        uint8_t displayBrightnessPercent);
  void drawFirmwareInfo();
  void showMessage(const char* title, const char* detail);
  void noteDisplayActivity();
  DisplaySleepUpdate updateDisplaySleep(bool enabled, uint8_t timeoutMinutes,
                                        uint8_t brightnessPercent,
                                        bool inhibitSleep);
  void resetButtonConfirmations();

  ButtonConfirmation buttonA_;
  ButtonConfirmation buttonB_;
  ButtonConfirmation buttonC_;
  unsigned long lastDisplayActivity_ = 0;
  unsigned long wakePressDetectedAt_ = 0;
  bool displaySleeping_ = false;
  bool wakeConfirmationPending_ = false;
};
