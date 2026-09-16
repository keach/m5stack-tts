#pragma once

#include <stddef.h>

// Feed bytes from EOF towards BOF. Only newline-terminated records are emitted.
template <size_t Capacity>
class ReverseNdjsonLine {
 public:
  void reset(bool endsWithNewline) {
    length_ = 0;
    discard_ = !endsWithNewline;
  }

  bool consume(char value, char* output) {
    if (value == '\n') {
      const bool emitted = emit(output);
      discard_ = false;
      length_ = 0;
      return emitted;
    }
    if (!discard_) {
      if (length_ + 1 < Capacity) {
        reversed_[length_++] = value;
      } else {
        discard_ = true;
        length_ = 0;
      }
    }
    return false;
  }

  bool finish(char* output) {
    const bool emitted = emit(output);
    length_ = 0;
    return emitted;
  }

 private:
  bool emit(char* output) const {
    if (discard_ || length_ == 0) return false;
    for (size_t index = 0; index < length_; ++index) {
      output[index] = reversed_[length_ - index - 1];
    }
    output[length_] = '\0';
    return true;
  }

  char reversed_[Capacity] = {};
  size_t length_ = 0;
  bool discard_ = false;
};
