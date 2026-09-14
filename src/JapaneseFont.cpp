#include "JapaneseFont.h"

#include <M5Stack.h>
#include <SD.h>

#include "SdCardLock.h"

namespace {
constexpr char FONT_NAME[] = "Japanese16";
constexpr char FONT_PATH[] = "/Japanese16.vlw";
constexpr int16_t LINE_HEIGHT = 22;

uint32_t readBigEndian32(File& file) {
  uint8_t bytes[4];
  return file.read(bytes, sizeof(bytes)) == sizeof(bytes)
             ? (static_cast<uint32_t>(bytes[0]) << 24) |
                   (static_cast<uint32_t>(bytes[1]) << 16) |
                   (static_cast<uint32_t>(bytes[2]) << 8) | bytes[3]
             : 0;
}

bool validateFontFile(File& file) {
  if (!file || file.size() < 24) return false;
  file.seek(0);
  const uint32_t glyphCount = readBigEndian32(file);
  const uint32_t version = readBigEndian32(file);
  const uint32_t fontSize = readBigEndian32(file);
  readBigEndian32(file);
  readBigEndian32(file);
  readBigEndian32(file);
  if (glyphCount == 0 || glyphCount > 5000 || version != 11 ||
      fontSize != 16 || file.size() < 24 + glyphCount * 28) {
    return false;
  }
  uint64_t expectedSize = 24 + glyphCount * 28;
  for (uint32_t index = 0; index < glyphCount; ++index) {
    const uint32_t codePoint = readBigEndian32(file);
    const uint32_t height = readBigEndian32(file);
    const uint32_t width = readBigEndian32(file);
    for (int field = 0; field < 4; ++field) readBigEndian32(file);
    if (codePoint > 0xffff || height > 64 || width > 64) return false;
    expectedSize += width * height;
  }
  return expectedSize <= file.size();
}
}

bool JapaneseFont::begin(bool storageAvailable) {
  if (!storageAvailable || !SD.exists(FONT_PATH)) {
    Serial.printf("Japanese font not found: %s. English fallback enabled.\n",
                  FONT_PATH);
    return false;
  }
  File file = SD.open(FONT_PATH, FILE_READ);
  if (!file) return false;
  fileSize_ = file.size();
  available_ = validateFontFile(file);
  file.close();
  if (!available_) {
    Serial.printf("Japanese font is invalid: %s. English fallback enabled.\n",
                  FONT_PATH);
  }
  if (!available_) return false;
  // Leave roughly 48 KiB available after the glyph metrics and line sprite
  // have been allocated so networking and JSON parsing retain working space.
  constexpr uint32_t REQUIRED_FREE_HEAP = 96U * 1024U;
  if (ESP.getFreeHeap() < REQUIRED_FREE_HEAP) {
    Serial.println("Japanese font load skipped because free heap is too low.");
    return false;
  }
  SdCardGuard guard(pdMS_TO_TICKS(1000));
  if (!guard.locked()) {
    Serial.println("Japanese font load skipped because the SD card is busy.");
    return false;
  }
  const uint32_t heapBefore = ESP.getFreeHeap();
  const uint32_t startedAt = millis();
  lineSprite_.setColorDepth(8);
  if (!lineSprite_.createSprite(320, LINE_HEIGHT)) {
    Serial.println("Japanese font line buffer allocation failed.");
    return false;
  }
  lineSprite_.loadFont(FONT_NAME, SD);
  loadTimeMs_ = millis() - startedAt;
  const uint32_t heapAfter = ESP.getFreeHeap();
  heapUsed_ = heapBefore > heapAfter ? heapBefore - heapAfter : 0;
  loaded_ = true;
  Serial.printf(
      "Japanese font ready. Size: %u bytes, heap: %u bytes, load: %u ms.\n",
      static_cast<unsigned>(fileSize_), static_cast<unsigned>(heapUsed_),
      static_cast<unsigned>(loadTimeMs_));
  return loaded_;
}

void JapaneseFont::drawLine(int16_t y, const char* text,
                            uint16_t foreground, uint16_t background,
                            int16_t x) {
  if (!loaded_ || !text) return;
  const uint32_t startedAt = millis();
  lineSprite_.fillSprite(background);
  lineSprite_.setTextColor(foreground, background);
  lineSprite_.setCursor(x, 0);
  lineSprite_.print(text);
  lineSprite_.pushSprite(0, y);
  if (!drawTimeLogged_) {
    Serial.printf("First Japanese line rendered in %u ms.\n",
                  static_cast<unsigned>(millis() - startedAt));
    drawTimeLogged_ = true;
  }
}
