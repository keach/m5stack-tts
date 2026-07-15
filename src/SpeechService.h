#pragma once

#include <Arduino.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class SpeechService {
 public:
  bool begin();
  bool speak(const char* utf8Text, int speed = 100);
  bool playAlertTone();
  void stop();
  bool isSpeaking() const;

 private:
  static void taskEntry(void* argument);
  void runTask();
  bool initializeAudio();

  TaskHandle_t taskHandle_ = nullptr;
  uint32_t* speechWorkBuffer_ = nullptr;
  uint8_t* languageWorkBuffer_ = nullptr;
  volatile bool speaking_ = false;
  volatile bool stopRequested_ = false;
  bool initialized_ = false;
};
