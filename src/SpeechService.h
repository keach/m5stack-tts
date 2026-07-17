#pragma once

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class SpeechService {
 public:
  static constexpr uint8_t DEFAULT_VOLUME_PERCENT = 50;

  bool begin();
  bool speak(const char* utf8Text, int speed = 100);
  bool playAlertTone();
  void stop();
  bool isSpeaking() const;
  void setVolumePercent(uint8_t volumePercent);
  uint8_t volumePercent() const;

 private:
  static void taskEntry(void* argument);
  void runTask();
  bool initializeAudio();
  int16_t applyVolume(int16_t sample) const;

  TaskHandle_t taskHandle_ = nullptr;
  uint32_t* speechWorkBuffer_ = nullptr;
  uint8_t* languageWorkBuffer_ = nullptr;
  volatile bool speaking_ = false;
  volatile bool stopRequested_ = false;
  volatile uint8_t volumePercent_ = DEFAULT_VOLUME_PERCENT;
  bool initialized_ = false;
};
