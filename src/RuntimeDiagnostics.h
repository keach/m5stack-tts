#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

inline void logRuntimeMemory(const char* stage) {
  Serial.printf(
      "Memory [%s]: free=%u, minimum=%u, largest=%u bytes.\n", stage,
      static_cast<unsigned>(ESP.getFreeHeap()),
      static_cast<unsigned>(ESP.getMinFreeHeap()),
      static_cast<unsigned>(
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

inline void logHeapIntegrity(const char* stage) {
  const bool valid = heap_caps_check_integrity_all(false);
  Serial.printf("Heap integrity [%s]: %s.\n", stage,
                valid ? "OK" : "CORRUPTED");
}
