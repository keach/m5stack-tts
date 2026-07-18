#pragma once

#include <stddef.h>

namespace SpeechNumberFormatter {

void formatInteger(int value, char* output, size_t outputSize);
void formatOneDecimal(float value, char* output, size_t outputSize);

}  // namespace SpeechNumberFormatter
