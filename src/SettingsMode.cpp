#include "SettingsMode.h"

namespace {
constexpr unsigned long BUTTON_CONFIRMATION_MS = 80;
constexpr int MENU_ITEM_COUNT = 6;

enum MenuItem {
  MENU_CLOCK,
  MENU_VOLUME,
  MENU_ALARM_TEST,
  MENU_SPEECH_TEST,
  MENU_DIAGNOSTICS,
  MENU_SAVE_AND_EXIT,
};
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

void SettingsMode::drawMenu(int selectedItem,
                            ClockDisplayPrecision clockPrecision,
                            uint8_t volumePercent) {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(42, 8);
  M5.Lcd.print("SETTINGS / DIAG");

  for (int item = 0; item < MENU_ITEM_COUNT; ++item) {
    const int y = 42 + item * 29;
    const bool selected = item == selectedItem;
    const uint16_t background = selected ? TFT_DARKCYAN : TFT_BLACK;
    M5.Lcd.fillRect(5, y - 3, 310, 25, background);
    M5.Lcd.setTextColor(selected ? TFT_WHITE : TFT_LIGHTGREY, background);
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
      case MENU_ALARM_TEST:
        M5.Lcd.print("Alarm test");
        break;
      case MENU_SPEECH_TEST:
        M5.Lcd.print("Speech test / stop");
        break;
      case MENU_DIAGNOSTICS:
        M5.Lcd.print("Diagnostics");
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

void SettingsMode::showDiagnostics(const DiagnosticStatus& diagnostics) {
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(88, 8);
  M5.Lcd.print("DIAGNOSTICS");

  const char* labels[] = {"microSD", "Dictionary", "Speech",
                          "Wi-Fi",  "NTP time",   "Weather"};
  const bool values[] = {
      diagnostics.storageAvailable,    diagnostics.dictionaryAvailable,
      diagnostics.speechAvailable,     diagnostics.wifiConnected,
      diagnostics.timeSynchronized,    diagnostics.weatherAvailable,
  };

  M5.Lcd.setTextSize(2);
  for (int index = 0; index < 6; ++index) {
    const int y = 45 + index * 27;
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(24, y);
    M5.Lcd.printf("%-12s", labels[index]);
    M5.Lcd.setTextColor(values[index] ? TFT_GREEN : TFT_RED, TFT_BLACK);
    M5.Lcd.setCursor(225, y);
    M5.Lcd.print(values[index] ? "OK" : "NG");
  }

  M5.Lcd.fillRect(0, 218, 320, 22, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(100, 225);
  M5.Lcd.print("Any button: back");

  while (true) {
    M5.update();
    if (confirmedPress(M5.BtnA, buttonA_) ||
        confirmedPress(M5.BtnB, buttonB_) ||
        confirmedPress(M5.BtnC, buttonC_)) {
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
  int selectedItem = MENU_CLOCK;
  speech.setVolumePercent(draftVolume);
  drawMenu(selectedItem, draftClockPrecision, draftVolume);

  while (true) {
    M5.update();
    const bool previousPressed = confirmedPress(M5.BtnA, buttonA_);
    const bool selectPressed = confirmedPress(M5.BtnB, buttonB_);
    const bool nextPressed = confirmedPress(M5.BtnC, buttonC_);

    if (previousPressed || nextPressed) {
      if (speech.isSpeaking()) {
        speech.stop();
      }
      selectedItem = previousPressed
                         ? (selectedItem + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT
                         : (selectedItem + 1) % MENU_ITEM_COUNT;
      drawMenu(selectedItem, draftClockPrecision, draftVolume);
    }

    if (selectPressed) {
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
        case MENU_ALARM_TEST:
          if (speechAvailable) {
            speech.playAlertTone();
          } else {
            showMessage("ALARM TEST", "Speech unavailable");
          }
          break;
        case MENU_SPEECH_TEST:
          if (!speechAvailable) {
            showMessage("SPEECH TEST", "Speech unavailable");
          } else if (speech.isSpeaking()) {
            speech.stop();
          } else {
            speech.speak("音声テストです。音量を確認してください。");
          }
          break;
        case MENU_DIAGNOSTICS:
          showDiagnostics(diagnostics);
          break;
        case MENU_SAVE_AND_EXIT:
          if (speech.isSpeaking()) {
            speech.stop();
          }
          settings.save(draftClockPrecision, draftVolume);
          showMessage("SETTINGS SAVED", "Returning to weather");
          return;
      }
      drawMenu(selectedItem, draftClockPrecision, draftVolume);
    }

    delay(10);
  }
}
