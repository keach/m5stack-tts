#include "SpeechNumberFormatter.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace {
void appendText(char* output, size_t outputSize, const char* text) {
  const size_t length = strlen(output);
  if (length >= outputSize) {
    return;
  }
  snprintf(output + length, outputSize - length, "%s", text);
}

void appendDigit(int digit, char* output, size_t outputSize) {
  static const char* const DIGITS[] = {"", "一", "二", "三", "四",
                                       "五", "六", "七", "八", "九"};
  appendText(output, outputSize, DIGITS[digit]);
}

void appendBelowTenThousand(unsigned int value, char* output,
                            size_t outputSize) {
  const int thousands = value / 1000;
  const int hundreds = value / 100 % 10;
  const int tens = value / 10 % 10;
  const int ones = value % 10;

  if (thousands > 0) {
    if (thousands > 1) appendDigit(thousands, output, outputSize);
    appendText(output, outputSize, "千");
  }
  if (hundreds > 0) {
    if (hundreds > 1) appendDigit(hundreds, output, outputSize);
    appendText(output, outputSize, "百");
  }
  if (tens > 0) {
    if (tens > 1) appendDigit(tens, output, outputSize);
    appendText(output, outputSize, "十");
  }
  if (ones > 0) {
    appendDigit(ones, output, outputSize);
  }
}
}  // namespace

namespace SpeechNumberFormatter {

void formatInteger(int value, char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  output[0] = '\0';

  if (value == 0) {
    appendText(output, outputSize, "れい");
    return;
  }
  const long long signedValue = value;
  if (signedValue < 0) {
    appendText(output, outputSize, "マイナス");
  }
  unsigned int magnitude = static_cast<unsigned int>(
      signedValue < 0 ? -signedValue : signedValue);

  const unsigned int hundredMillions = magnitude / 100000000U;
  if (hundredMillions > 0) {
    appendBelowTenThousand(hundredMillions, output, outputSize);
    appendText(output, outputSize, "億");
    magnitude %= 100000000U;
  }
  const unsigned int tenThousands = magnitude / 10000U;
  if (tenThousands > 0) {
    appendBelowTenThousand(tenThousands, output, outputSize);
    appendText(output, outputSize, "万");
  }
  appendBelowTenThousand(magnitude % 10000U, output, outputSize);
}

void formatOneDecimal(float value, char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  if (lroundf(value * 10.0F) == 0) {
    snprintf(output, outputSize, "れい");
    return;
  }
  snprintf(output, outputSize, "%.1f", value);
}

}  // namespace SpeechNumberFormatter
