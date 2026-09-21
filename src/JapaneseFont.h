#pragma once

#include <M5Stack.h>

class JapaneseFont {
 public:
  bool begin(bool storageAvailable);
  bool suspendForNetworkRequest();
  bool resumeAfterNetworkRequest();
  bool available() const { return available_; }
  bool loaded() const { return loaded_; }
  size_t fileSize() const { return fileSize_; }
  uint32_t heapUsed() const { return heapUsed_; }
  uint32_t loadTimeMs() const { return loadTimeMs_; }
  void logRenderingState(const char* stage) const;
  void drawLine(int16_t y, const char* text, uint16_t foreground,
                uint16_t background, int16_t x = 16);
  void drawLineEllipsized(int16_t y, const char* text, uint16_t foreground,
                          uint16_t background, int16_t x = 16);

 private:
  bool available_ = false;
  bool loaded_ = false;
  size_t fileSize_ = 0;
  uint32_t heapUsed_ = 0;
  uint32_t loadTimeMs_ = 0;
  TFT_eSprite lineSprite_{&M5.Lcd};
  bool drawTimeLogged_ = false;
  bool suspended_ = false;
  uint16_t* glyphCodes_ = nullptr;
  uint32_t glyphCount_ = 0;

  bool loadGlyphCodes();
  bool canSafelyLoadFontMetrics() const;
  bool hasGlyph(uint32_t codePoint) const;
  String sanitize(const char* text) const;
};
