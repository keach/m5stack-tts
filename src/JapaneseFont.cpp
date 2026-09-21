#include "JapaneseFont.h"

#include <M5Stack.h>
#include <SD.h>

#include "SdCardLock.h"
#include "RuntimeDiagnostics.h"

namespace {
constexpr char FONT_NAME[] = "Japanese16";
constexpr char FONT_PATH[] = "/Japanese16.vlw";
constexpr int16_t LINE_HEIGHT = 22;
// TFT_eSPI::loadMetrics() allocates seven arrays per glyph: 2 + 1 + 1 + 1 +
// 2 + 1 + 4 bytes. It does not check any allocation result before dereference.
constexpr uint32_t METRIC_BYTES_PER_GLYPH = 12;
constexpr uint32_t METRIC_ALLOCATION_MARGIN = 2U * 1024U;
constexpr uint32_t POST_LOAD_FREE_HEAP_RESERVE = 32U * 1024U;

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
  if (!loadGlyphCodes()) {
    Serial.println("Japanese font glyph index allocation failed.");
    return false;
  }
  lineSprite_.setColorDepth(8);
  if (!lineSprite_.createSprite(320, LINE_HEIGHT)) {
    Serial.println("Japanese font line buffer allocation failed.");
    free(glyphCodes_);
    glyphCodes_ = nullptr;
    glyphCount_ = 0;
    return false;
  }
  if (!canSafelyLoadFontMetrics()) {
    lineSprite_.deleteSprite();
    free(glyphCodes_);
    glyphCodes_ = nullptr;
    glyphCount_ = 0;
    return false;
  }
  lineSprite_.loadFont(FONT_NAME, SD);
  if (!lineSprite_.fontLoaded || !lineSprite_.fontFile) {
    Serial.println("Japanese font load failed.");
    lineSprite_.unloadFont();
    lineSprite_.deleteSprite();
    free(glyphCodes_);
    glyphCodes_ = nullptr;
    glyphCount_ = 0;
    return false;
  }
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

bool JapaneseFont::loadGlyphCodes() {
  File file = SD.open(FONT_PATH, FILE_READ);
  if (!file) return false;
  const uint32_t count = readBigEndian32(file);
  for (int field = 0; field < 5; ++field) readBigEndian32(file);
  if (count == 0 || count > 5000) {
    file.close();
    return false;
  }
  glyphCodes_ = static_cast<uint16_t*>(malloc(count * sizeof(uint16_t)));
  if (!glyphCodes_) {
    file.close();
    return false;
  }
  for (uint32_t index = 0; index < count; ++index) {
    const uint32_t codePoint = readBigEndian32(file);
    glyphCodes_[index] = static_cast<uint16_t>(codePoint);
    if (!file.seek(file.position() + 24)) {
      free(glyphCodes_);
      glyphCodes_ = nullptr;
      file.close();
      return false;
    }
  }
  glyphCount_ = count;
  file.close();
  return true;
}

bool JapaneseFont::suspendForNetworkRequest() {
  if (!loaded_ || suspended_) return false;
  SdCardGuard guard(pdMS_TO_TICKS(1000));
  if (!guard.locked()) {
    Serial.println("Japanese font suspend skipped because the SD card is busy.");
    return false;
  }
  lineSprite_.unloadFont();
  loaded_ = false;
  suspended_ = true;
  Serial.println("Japanese font suspended for HTTPS requests.");
  logRuntimeMemory("Japanese font suspended");
  return true;
}

bool JapaneseFont::resumeAfterNetworkRequest() {
  if (!suspended_) return loaded_;
  if (!canSafelyLoadFontMetrics()) {
    Serial.println(
        "Japanese font reload deferred because the heap is too fragmented.");
    return false;
  }
  SdCardGuard guard(pdMS_TO_TICKS(1000));
  if (!guard.locked()) {
    Serial.println("Japanese font reload deferred because the SD card is busy.");
    return false;
  }
  lineSprite_.loadFont(FONT_NAME, SD);
  if (!lineSprite_.fontLoaded || !lineSprite_.fontFile) {
    loaded_ = false;
    suspended_ = true;
    Serial.println("Japanese font reload failed.");
    return false;
  }
  loaded_ = true;
  suspended_ = false;
  Serial.println("Japanese font reloaded after HTTPS requests.");
  logRuntimeMemory("Japanese font reloaded");
  return true;
}

bool JapaneseFont::canSafelyLoadFontMetrics() const {
  if (glyphCount_ == 0) {
    Serial.println("Japanese font metrics are unavailable.");
    return false;
  }

  const uint32_t metricBytes =
      glyphCount_ * METRIC_BYTES_PER_GLYPH + METRIC_ALLOCATION_MARGIN;
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock = ESP.getMaxAllocHeap();
  if (freeHeap < metricBytes + POST_LOAD_FREE_HEAP_RESERVE ||
      largestBlock < metricBytes) {
    Serial.printf(
        "Japanese font reload deferred: free=%u, largest=%u, required=%u "
        "bytes.\n",
        static_cast<unsigned>(freeHeap), static_cast<unsigned>(largestBlock),
        static_cast<unsigned>(metricBytes));
    return false;
  }

  // Probe the contiguous region before entering TFT_eSPI::loadMetrics().
  // This protects against the library dereferencing a null allocation result.
  void* probe = malloc(metricBytes);
  if (!probe) {
    Serial.printf(
        "Japanese font reload deferred: unable to reserve %u metric bytes.\n",
        static_cast<unsigned>(metricBytes));
    return false;
  }
  free(probe);
  return true;
}

bool JapaneseFont::hasGlyph(uint32_t codePoint) const {
  if (codePoint < 0x80) return true;
  for (uint32_t index = 0; index < glyphCount_; ++index) {
    if (glyphCodes_[index] == codePoint) return true;
  }
  return false;
}

void JapaneseFont::logRenderingState(const char* stage) const {
  Serial.printf(
      "Japanese font state [%s]: available=%s, loaded=%s, suspended=%s, "
      "spriteFont=%s, fontFile=%s, glyphs=%u.\n",
      stage, available_ ? "yes" : "no", loaded_ ? "yes" : "no",
      suspended_ ? "yes" : "no", lineSprite_.fontLoaded ? "yes" : "no",
      lineSprite_.fontFile ? "open" : "closed",
      static_cast<unsigned>(glyphCount_));
  logHeapIntegrity(stage);
}

String JapaneseFont::sanitize(const char* text) const {
  String result;
  if (!text) return result;
  const uint8_t* cursor = reinterpret_cast<const uint8_t*>(text);
  while (*cursor) {
    const uint8_t* start = cursor;
    uint32_t codePoint = 0;
    size_t length = 1;
    if (*cursor < 0x80) {
      codePoint = *cursor;
    } else if ((*cursor & 0xe0) == 0xc0 && cursor[1]) {
      codePoint = ((*cursor & 0x1f) << 6) | (cursor[1] & 0x3f);
      length = 2;
    } else if ((*cursor & 0xf0) == 0xe0 && cursor[1] && cursor[2]) {
      codePoint = ((*cursor & 0x0f) << 12) | ((cursor[1] & 0x3f) << 6) |
                  (cursor[2] & 0x3f);
      length = 3;
    } else if ((*cursor & 0xf8) == 0xf0 && cursor[1] && cursor[2] &&
               cursor[3]) {
      codePoint = 0x10000;
      length = 4;
    }
    if (hasGlyph(codePoint)) {
      for (size_t index = 0; index < length; ++index) {
        result += static_cast<char>(start[index]);
      }
    } else {
      result += "〓";
    }
    cursor += length;
  }
  return result;
}

void JapaneseFont::drawLine(int16_t y, const char* text,
                            uint16_t foreground, uint16_t background,
                            int16_t x) {
  if (!loaded_ || !text) return;
  SdCardGuard guard(pdMS_TO_TICKS(1000));
  if (!guard.locked()) {
    Serial.println("Japanese font draw skipped because the SD card is busy.");
    return;
  }
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

void JapaneseFont::drawLineEllipsized(int16_t y, const char* text,
                                      uint16_t foreground,
                                      uint16_t background, int16_t x) {
  if (!loaded_ || !text) return;
  String fitted = sanitize(text);
  const int16_t availableWidth = max<int16_t>(0, 320 - x);
  if (lineSprite_.textWidth(fitted) > availableWidth) {
    while (!fitted.isEmpty() &&
           lineSprite_.textWidth(fitted + "...") > availableWidth) {
      int last = fitted.length() - 1;
      while (last > 0 &&
             (static_cast<uint8_t>(fitted[last]) & 0xc0) == 0x80) {
        --last;
      }
      fitted.remove(last);
    }
    fitted += "...";
  }
  drawLine(y, fitted.c_str(), foreground, background, x);
}
