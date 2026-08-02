#include "SdCardLock.h"

SemaphoreHandle_t SdCardLock::mutex() {
  static SemaphoreHandle_t instance = xSemaphoreCreateMutex();
  return instance;
}
bool SdCardLock::acquire(TickType_t timeoutTicks) {
  SemaphoreHandle_t instance = mutex();
  return instance != nullptr && xSemaphoreTake(instance, timeoutTicks) == pdTRUE;
}
void SdCardLock::release() {
  SemaphoreHandle_t instance = mutex();
  if (instance != nullptr) xSemaphoreGive(instance);
}
