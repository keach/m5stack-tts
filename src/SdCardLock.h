#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class SdCardLock {
 public:
  static bool acquire(TickType_t timeoutTicks = pdMS_TO_TICKS(50));
  static void release();
 private:
  static SemaphoreHandle_t mutex();
};

class SdCardGuard {
 public:
  explicit SdCardGuard(TickType_t timeoutTicks = pdMS_TO_TICKS(50))
      : locked_(SdCardLock::acquire(timeoutTicks)) {}
  ~SdCardGuard() { if (locked_) SdCardLock::release(); }
  bool locked() const { return locked_; }
 private:
  bool locked_;
};
