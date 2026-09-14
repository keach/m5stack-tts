#include "SettingsMode.h"

#include "FirmwareInfo.h"

namespace {
constexpr unsigned long BUTTON_CONFIRMATION_MS = 80;
constexpr int MENU_ROWS_PER_PAGE = 5;

enum MenuItem {
  MENU_CLOCK,
  MENU_VOLUME,
  MENU_DISPLAY_SLEEP,
  MENU_DISPLAY_SLEEP_TIMEOUT,
  MENU_DISPLAY_BRIGHTNESS,
  MENU_FORECAST_1_ENABLED,
  MENU_FORECAST_1_HOUR,
  MENU_FORECAST_1_MINUTE,
  MENU_FORECAST_2_ENABLED,
  MENU_FORECAST_2_HOUR,
  MENU_FORECAST_2_MINUTE,
  MENU_FORECAST_3_ENABLED,
  MENU_FORECAST_3_HOUR,
  MENU_FORECAST_3_MINUTE,
  MENU_ALARM_TEST,
  MENU_SPEECH_TEST,
  MENU_DIAGNOSTICS,
  MENU_FIRMWARE_INFO,
  MENU_SAVE_AND_EXIT,
};
constexpr int MENU_ITEM_COUNT = MENU_SAVE_AND_EXIT + 1;

constexpr MenuItem MENU_ROW_ITEMS[] = {
    MENU_CLOCK,              MENU_VOLUME,          MENU_DISPLAY_SLEEP,
    MENU_DISPLAY_SLEEP_TIMEOUT, MENU_DISPLAY_BRIGHTNESS,
    MENU_FORECAST_1_ENABLED,
    MENU_FORECAST_2_ENABLED, MENU_FORECAST_3_ENABLED,
    MENU_ALARM_TEST,         MENU_SPEECH_TEST,     MENU_DIAGNOSTICS,
    MENU_FIRMWARE_INFO,      MENU_SAVE_AND_EXIT,
};
constexpr int MENU_ROW_COUNT = sizeof(MENU_ROW_ITEMS) / sizeof(MenuItem);
constexpr int MENU_PAGE_COUNT =
    (MENU_ROW_COUNT + MENU_ROWS_PER_PAGE - 1) / MENU_ROWS_PER_PAGE;

int forecastScheduleIndex(int item) {
  return (item - MENU_FORECAST_1_ENABLED) / 3;
}

int forecastFieldIndex(int item) {
  return (item - MENU_FORECAST_1_ENABLED) % 3;
}

bool isForecastMenuItem(int item) {
  return item >= MENU_FORECAST_1_ENABLED && item <= MENU_FORECAST_3_MINUTE;
}

int menuRow(int item) {
  for (int row = 0; row < MENU_ROW_COUNT; ++row) {
    const int rowItem = MENU_ROW_ITEMS[row];
    if (rowItem == item ||
        (isForecastMenuItem(rowItem) && isForecastMenuItem(item) &&
         forecastScheduleIndex(rowItem) == forecastScheduleIndex(item))) {
      return row;
    }
  }
  return 0;
}

void disableDuplicateForecastSchedules(
    AppSettings::ForecastSchedule* schedules) {
  for (size_t index = 0; index < AppSettings::FORECAST_SCHEDULE_COUNT;
       ++index) {
    if (!schedules[index].enabled) {
      continue;
    }
    for (size_t previous = 0; previous < index; ++previous) {
      if (schedules[previous].enabled &&
          schedules[previous].minuteOfDay == schedules[index].minuteOfDay) {
        schedules[index].enabled = false;
        break;
      }
    }
  }
}
}  // namespace

bool SettingsMode::confirmedPress(Button& button,
                                  ButtonConfirmation& confirmation) {
  if (button.wasPressed()) {
    confirmation.pending = true;
    confirmation.detectedAt = millis();
  }
  if (!confirmation.pending) {
    return false;
  }
  if (button.isReleased()) {
    confirmation.pending = false;
    return false;
  }
  if (millis() - confirmation.detectedAt < BUTTON_CONFIRMATION_MS) {
    return false;
  }
  confirmation.pending = false;
  return true;
}

void SettingsMode::noteDisplayActivity() { lastDisplayActivity_ = millis(); }

void SettingsMode::resetButtonConfirmations() {
  buttonA_.pending = false;
  buttonB_.pending = false;
  buttonC_.pending = false;
}

SettingsMode::DisplaySleepUpdate SettingsMode::updateDisplaySleep(
    bool enabled, uint8_t timeoutMinutes, uint8_t brightnessPercent,
    bool inhibitSleep) {
  const bool buttonPressed =
      M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnC.isPressed();
  if (displaySleeping_) {
    if (!wakeConfirmationPending_ && buttonPressed) {
      wakeConfirmationPending_ = true;
      wakePressDetectedAt_ = millis();
    }
    if (wakeConfirmationPending_) {
      if (!buttonPressed) {
        wakeConfirmationPending_ = false;
      } else if (millis() - wakePressDetectedAt_ >= BUTTON_CONFIRMATION_MS) {
        wakeConfirmationPending_ = false;
        displaySleeping_ = false;
        resetButtonConfirmations();
        M5.Lcd.wakeup();
        M5.Lcd.setBrightness(
            AppSettings::displayBrightnessLevel(brightnessPercent));
        noteDisplayActivity();
        Serial.println("Settings display woke up (reason: button).");
        return DisplaySleepUpdate::Woke;
      }
    }
    return DisplaySleepUpdate::Sleeping;
  }

  const unsigned long timeoutMs =
      static_cast<unsigned long>(timeoutMinutes) * 60UL * 1000UL;
  if (enabled && !inhibitSleep && !buttonPressed &&
      millis() - lastDisplayActivity_ >= timeoutMs) {
    displaySleeping_ = true;
    wakeConfirmationPending_ = false;
    M5.Lcd.setBrightness(0);
    M5.Lcd.sleep();
    Serial.println("Settings display entered sleep mode.");
    return DisplaySleepUpdate::Sleeping;
  }
  return DisplaySleepUpdate::Awake;
}

void SettingsMode::drawMenu(int selectedItem,
                            ClockDisplayPrecision clockPrecision,
                            uint8_t volumePercent,
                            bool displaySleepEnabled,
                            uint8_t displaySleepMinutes,
                            uint8_t displayBrightnessPercent,
                            const AppSettings::ForecastSchedule*
                                forecastSchedules) {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  const int currentPage = menuRow(selectedItem) / MENU_ROWS_PER_PAGE;
  char title[24];
  snprintf(title, sizeof(title), "SETTINGS %d/%d", currentPage + 1,
           MENU_PAGE_COUNT);
  M5.Lcd.setCursor(max(0, (320 - M5.Lcd.textWidth(title)) / 2), 8);
  M5.Lcd.print(title);

  const int firstRow = currentPage * MENU_ROWS_PER_PAGE;
  const int lastRow = min(firstRow + MENU_ROWS_PER_PAGE, MENU_ROW_COUNT);
  for (int menuRowIndex = firstRow; menuRowIndex < lastRow; ++menuRowIndex) {
    const int row = menuRowIndex - firstRow;
    const int item = MENU_ROW_ITEMS[menuRowIndex];
    const int y = 43 + row * 34;
    const bool forecastItem = isForecastMenuItem(item);
    const bool selected = forecastItem
                              ? isForecastMenuItem(selectedItem) &&
                                    forecastScheduleIndex(item) ==
                                        forecastScheduleIndex(selectedItem)
                              : item == selectedItem;
    const uint16_t background = selected ? TFT_DARKCYAN : TFT_BLACK;
    M5.Lcd.fillRect(5, y - 3, 310, 27,
                    forecastItem ? TFT_BLACK : background);
    M5.Lcd.setTextColor(
        selected && !forecastItem ? TFT_WHITE : TFT_LIGHTGREY,
        forecastItem ? TFT_BLACK : background);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(10, y);

    switch (item) {
      case MENU_CLOCK:
        M5.Lcd.printf("Clock: %s",
                      clockPrecision == ClockDisplayPrecision::Seconds
                          ? "Seconds"
                          : "Minutes");
        break;
      case MENU_VOLUME:
        M5.Lcd.printf("Volume: %u%%", volumePercent);
        break;
      case MENU_DISPLAY_SLEEP:
        M5.Lcd.printf("Display sleep: %s",
                      displaySleepEnabled ? "On" : "Off");
        break;
      case MENU_DISPLAY_SLEEP_TIMEOUT:
        M5.Lcd.printf("Sleep after: %u min", displaySleepMinutes);
        break;
      case MENU_DISPLAY_BRIGHTNESS:
        M5.Lcd.printf("Brightness: %u%%", displayBrightnessPercent);
        break;
      case MENU_FORECAST_1_ENABLED:
      case MENU_FORECAST_2_ENABLED:
      case MENU_FORECAST_3_ENABLED: {
        const int scheduleIndex = forecastScheduleIndex(item);
        const AppSettings::ForecastSchedule& schedule =
            forecastSchedules[scheduleIndex];
        const uint16_t hour = schedule.minuteOfDay / 60;
        const uint16_t minute = schedule.minuteOfDay % 60;
        const int selectedField = selected ? forecastFieldIndex(selectedItem)
                                           : -1;
        M5.Lcd.printf("Forecast %d: ", scheduleIndex + 1);
        const int enabledX = M5.Lcd.getCursorX();
        const char* enabledText = schedule.enabled ? "On " : "Off";
        M5.Lcd.fillRect(enabledX - 2, y - 3, 40, 27,
                        selectedField == 0 ? TFT_DARKCYAN : TFT_BLACK);
        M5.Lcd.setTextColor(selectedField == 0 ? TFT_WHITE : TFT_LIGHTGREY,
                            selectedField == 0 ? TFT_DARKCYAN : TFT_BLACK);
        M5.Lcd.setCursor(enabledX, y);
        M5.Lcd.print(enabledText);

        const int hourX = enabledX + 48;
        M5.Lcd.fillRect(hourX - 2, y - 3, 28, 27,
                        selectedField == 1 ? TFT_DARKCYAN : TFT_BLACK);
        M5.Lcd.setTextColor(selectedField == 1 ? TFT_WHITE : TFT_LIGHTGREY,
                            selectedField == 1 ? TFT_DARKCYAN : TFT_BLACK);
        M5.Lcd.setCursor(hourX, y);
        M5.Lcd.printf("%02u", hour);

        M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        M5.Lcd.setCursor(hourX + 27, y);
        M5.Lcd.print(":");
        const int minuteX = hourX + 39;
        M5.Lcd.fillRect(minuteX - 2, y - 3, 28, 27,
                        selectedField == 2 ? TFT_DARKCYAN : TFT_BLACK);
        M5.Lcd.setTextColor(selectedField == 2 ? TFT_WHITE : TFT_LIGHTGREY,
                            selectedField == 2 ? TFT_DARKCYAN : TFT_BLACK);
        M5.Lcd.setCursor(minuteX, y);
        M5.Lcd.printf("%02u", minute);
        break;
      }
      case MENU_ALARM_TEST:
        M5.Lcd.print("Alarm test");
        break;
      case MENU_SPEECH_TEST:
        M5.Lcd.print("Speech test / stop");
        break;
      case MENU_DIAGNOSTICS:
        M5.Lcd.print("Diagnostics");
        break;
      case MENU_FIRMWARE_INFO:
        M5.Lcd.print("Firmware info");
        break;
      case MENU_SAVE_AND_EXIT:
        M5.Lcd.print("Save and exit");
        break;
    }
  }

  M5.Lcd.fillRect(0, 218, 320, 22, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(56, 225);
  M5.Lcd.print("A: prev   B: select   C: next");
}

void SettingsMode::showMessage(const char* title, const char* detail) {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20, 76);
  M5.Lcd.print(title);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(20, 116);
  M5.Lcd.print(detail);

  const unsigned long startedAt = millis();
  while (millis() - startedAt < 800) {
    M5.update();
    delay(10);
  }
}

void SettingsMode::drawDiagnostics(const DiagnosticStatus& diagnostics) {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(88, 8);
  M5.Lcd.print("DIAGNOSTICS");

  const char* labels[] = {"microSD", "Dictionary", "Speech", "JP font",
                          "Wi-Fi", "NTP time", "Weather"};
  const bool values[] = {
      diagnostics.storageAvailable,    diagnostics.dictionaryAvailable,
      diagnostics.speechAvailable,     diagnostics.japaneseFontAvailable,
      diagnostics.wifiConnected,
      diagnostics.timeSynchronized,    diagnostics.weatherAvailable,
  };

  M5.Lcd.setTextSize(2);
  for (int index = 0; index < 7; ++index) {
    const int y = 40 + index * 24;
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(24, y);
    M5.Lcd.printf("%-12s", labels[index]);
    M5.Lcd.setTextColor(values[index] ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Lcd.setCursor(225, y);
    M5.Lcd.print(values[index] ? "OK" : "NG");
  }

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setCursor(24, 207);
  M5.Lcd.printf("IP: %s", diagnostics.ipAddress.toString().c_str());

  M5.Lcd.fillRect(0, 218, 320, 22, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(100, 225);
  M5.Lcd.print("Any button: back");
}

void SettingsMode::drawFirmwareInfo() {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(76, 8);
  M5.Lcd.print("FIRMWARE INFO");

  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setCursor(16, 56);
  M5.Lcd.print("Version");
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(16, 82);
  M5.Lcd.print(FIRMWARE_VERSION);

  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setCursor(16, 126);
  M5.Lcd.print("Commit Date");
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(16, 152);
  M5.Lcd.print(FIRMWARE_COMMIT_DATE);

  M5.Lcd.fillRect(0, 218, 320, 22, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(100, 225);
  M5.Lcd.print("Any button: back");
}

void SettingsMode::showDiagnostics(const DiagnosticStatus& diagnostics,
                                   bool displaySleepEnabled,
                                   uint8_t displaySleepMinutes,
                                   uint8_t displayBrightnessPercent) {
  noteDisplayActivity();
  drawDiagnostics(diagnostics);

  while (true) {
    M5.update();
    const DisplaySleepUpdate sleepUpdate = updateDisplaySleep(
        displaySleepEnabled, displaySleepMinutes, displayBrightnessPercent,
        false);
    if (sleepUpdate == DisplaySleepUpdate::Woke) {
      drawDiagnostics(diagnostics);
      delay(10);
      continue;
    }
    if (sleepUpdate == DisplaySleepUpdate::Sleeping) {
      delay(10);
      continue;
    }
    if (confirmedPress(M5.BtnA, buttonA_) ||
        confirmedPress(M5.BtnB, buttonB_) ||
        confirmedPress(M5.BtnC, buttonC_)) {
      noteDisplayActivity();
      return;
    }
    delay(10);
  }
}

void SettingsMode::showFirmwareInfo(bool displaySleepEnabled,
                                    uint8_t displaySleepMinutes,
                                    uint8_t displayBrightnessPercent) {
  noteDisplayActivity();
  drawFirmwareInfo();

  while (true) {
    M5.update();
    const DisplaySleepUpdate sleepUpdate = updateDisplaySleep(
        displaySleepEnabled, displaySleepMinutes, displayBrightnessPercent,
        false);
    if (sleepUpdate == DisplaySleepUpdate::Woke) {
      drawFirmwareInfo();
      delay(10);
      continue;
    }
    if (sleepUpdate == DisplaySleepUpdate::Sleeping) {
      delay(10);
      continue;
    }
    if (confirmedPress(M5.BtnA, buttonA_) ||
        confirmedPress(M5.BtnB, buttonB_) ||
        confirmedPress(M5.BtnC, buttonC_)) {
      noteDisplayActivity();
      return;
    }
    delay(10);
  }
}

void SettingsMode::run(AppSettings& settings, SpeechService& speech,
                       bool speechAvailable,
                       const DiagnosticStatus& diagnostics) {
  ClockDisplayPrecision draftClockPrecision = settings.clockPrecision();
  uint8_t draftVolume = settings.volumePercent();
  bool draftDisplaySleepEnabled = settings.displaySleepEnabled();
  uint8_t draftDisplaySleepMinutes = settings.displaySleepMinutes();
  uint8_t draftDisplayBrightnessPercent =
      settings.displayBrightnessPercent();
  AppSettings::ForecastSchedule
      draftForecastSchedules[AppSettings::FORECAST_SCHEDULE_COUNT];
  for (size_t index = 0; index < AppSettings::FORECAST_SCHEDULE_COUNT;
       ++index) {
    draftForecastSchedules[index] = settings.forecastSchedule(index);
  }
  int selectedItem = MENU_CLOCK;
  displaySleeping_ = false;
  wakeConfirmationPending_ = false;
  resetButtonConfirmations();
  noteDisplayActivity();
  speech.setVolumePercent(draftVolume);
  M5.Lcd.setBrightness(
      AppSettings::displayBrightnessLevel(draftDisplayBrightnessPercent));
  drawMenu(selectedItem, draftClockPrecision, draftVolume,
           draftDisplaySleepEnabled, draftDisplaySleepMinutes,
           draftDisplayBrightnessPercent,
           draftForecastSchedules);
  bool speechWasActive = speech.isSpeaking();

  while (true) {
    M5.update();
    const bool speechActive = speech.isSpeaking();
    if (speechWasActive && !speechActive) {
      noteDisplayActivity();
    }
    speechWasActive = speechActive;
    const DisplaySleepUpdate sleepUpdate = updateDisplaySleep(
        draftDisplaySleepEnabled, draftDisplaySleepMinutes,
        draftDisplayBrightnessPercent, speechActive);
    if (sleepUpdate == DisplaySleepUpdate::Woke) {
      drawMenu(selectedItem, draftClockPrecision, draftVolume,
               draftDisplaySleepEnabled, draftDisplaySleepMinutes,
               draftDisplayBrightnessPercent, draftForecastSchedules);
      delay(10);
      continue;
    }
    if (sleepUpdate == DisplaySleepUpdate::Sleeping) {
      delay(10);
      continue;
    }
    const bool previousPressed = confirmedPress(M5.BtnA, buttonA_);
    const bool selectPressed = confirmedPress(M5.BtnB, buttonB_);
    const bool nextPressed = confirmedPress(M5.BtnC, buttonC_);

    if (previousPressed || nextPressed) {
      noteDisplayActivity();
      if (speech.isSpeaking()) {
        speech.stop();
      }
      selectedItem = previousPressed
                         ? (selectedItem + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT
                         : (selectedItem + 1) % MENU_ITEM_COUNT;
      drawMenu(selectedItem, draftClockPrecision, draftVolume,
               draftDisplaySleepEnabled, draftDisplaySleepMinutes,
               draftDisplayBrightnessPercent,
               draftForecastSchedules);
    }

    if (selectPressed) {
      noteDisplayActivity();
      switch (selectedItem) {
        case MENU_CLOCK:
          draftClockPrecision =
              draftClockPrecision == ClockDisplayPrecision::Minutes
                  ? ClockDisplayPrecision::Seconds
                  : ClockDisplayPrecision::Minutes;
          break;
        case MENU_VOLUME:
          draftVolume = draftVolume >= 100 ? 0 : draftVolume + 10;
          speech.setVolumePercent(draftVolume);
          break;
        case MENU_DISPLAY_SLEEP:
          draftDisplaySleepEnabled = !draftDisplaySleepEnabled;
          break;
        case MENU_DISPLAY_SLEEP_TIMEOUT:
          if (draftDisplaySleepMinutes == 1) {
            draftDisplaySleepMinutes = 5;
          } else if (draftDisplaySleepMinutes == 5) {
            draftDisplaySleepMinutes = 10;
          } else if (draftDisplaySleepMinutes == 10) {
            draftDisplaySleepMinutes = 30;
          } else {
            draftDisplaySleepMinutes = 1;
          }
          break;
        case MENU_DISPLAY_BRIGHTNESS:
          draftDisplayBrightnessPercent =
              draftDisplayBrightnessPercent >= 100
                  ? 20
                  : draftDisplayBrightnessPercent + 20;
          M5.Lcd.setBrightness(AppSettings::displayBrightnessLevel(
              draftDisplayBrightnessPercent));
          break;
        case MENU_FORECAST_1_ENABLED:
        case MENU_FORECAST_1_HOUR:
        case MENU_FORECAST_1_MINUTE:
        case MENU_FORECAST_2_ENABLED:
        case MENU_FORECAST_2_HOUR:
        case MENU_FORECAST_2_MINUTE:
        case MENU_FORECAST_3_ENABLED:
        case MENU_FORECAST_3_HOUR:
        case MENU_FORECAST_3_MINUTE: {
          AppSettings::ForecastSchedule& schedule =
              draftForecastSchedules[forecastScheduleIndex(selectedItem)];
          const int fieldIndex = forecastFieldIndex(selectedItem);
          if (fieldIndex == 0) {
            schedule.enabled = !schedule.enabled;
          } else if (fieldIndex == 1) {
            const uint16_t hour = (schedule.minuteOfDay / 60 + 1) % 24;
            schedule.minuteOfDay = hour * 60 + schedule.minuteOfDay % 60;
          } else {
            const uint16_t minute = (schedule.minuteOfDay % 60 + 15) % 60;
            schedule.minuteOfDay = (schedule.minuteOfDay / 60) * 60 + minute;
          }
          break;
        }
        case MENU_ALARM_TEST:
          if (speechAvailable) {
            speech.playAlertTone();
          } else {
            showMessage("ALARM TEST", "Speech unavailable");
          }
          noteDisplayActivity();
          break;
        case MENU_SPEECH_TEST:
          if (!speechAvailable) {
            showMessage("SPEECH TEST", "Speech unavailable");
          } else if (speech.isSpeaking()) {
            speech.stop();
          } else {
            speech.speak("音声テストです。音量を確認してください。");
          }
          noteDisplayActivity();
          break;
        case MENU_DIAGNOSTICS:
          showDiagnostics(diagnostics, draftDisplaySleepEnabled,
                          draftDisplaySleepMinutes,
                          draftDisplayBrightnessPercent);
          noteDisplayActivity();
          break;
        case MENU_FIRMWARE_INFO:
          showFirmwareInfo(draftDisplaySleepEnabled,
                           draftDisplaySleepMinutes,
                           draftDisplayBrightnessPercent);
          noteDisplayActivity();
          break;
        case MENU_SAVE_AND_EXIT:
          if (speech.isSpeaking()) {
            speech.stop();
          }
          disableDuplicateForecastSchedules(draftForecastSchedules);
          settings.save(draftClockPrecision, draftVolume,
                        draftDisplaySleepEnabled, draftDisplaySleepMinutes,
                        draftDisplayBrightnessPercent,
                        draftForecastSchedules);
          showMessage("SETTINGS SAVED", "Returning to weather");
          return;
      }
      drawMenu(selectedItem, draftClockPrecision, draftVolume,
               draftDisplaySleepEnabled, draftDisplaySleepMinutes,
               draftDisplayBrightnessPercent,
               draftForecastSchedules);
    }

    delay(10);
  }
}
