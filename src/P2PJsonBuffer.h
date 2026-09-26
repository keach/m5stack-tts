#pragma once

#include <ArduinoJson.h>

#include <cstddef>
#include <cstring>

// ArduinoJson 7's StaticJsonDocument is retained only as a deprecated
// compatibility wrapper; it still uses the default heap allocator. This
// monotonic allocator owns a caller-provided fixed arena instead.
template <size_t Capacity>
class FixedJsonArenaAllocator final : public ArduinoJson::Allocator {
 public:
  void reset() { used_ = 0; }
  size_t used() const { return used_; }
  static constexpr size_t capacity() { return Capacity; }

  void* allocate(size_t size) override {
    if (size == 0) size = 1;
    const size_t headerOffset = alignUp(used_);
    const size_t dataOffset = headerOffset + sizeof(BlockHeader);
    if (dataOffset > Capacity || size > Capacity - dataOffset) return nullptr;
    auto* header = reinterpret_cast<BlockHeader*>(storage_ + headerOffset);
    header->size = size;
    used_ = dataOffset + size;
    return storage_ + dataOffset;
  }

  void deallocate(void* /*pointer*/) override {
    // The complete document is discarded together, via reset().
  }

  void* reallocate(void* pointer, size_t newSize) override {
    if (!pointer) return allocate(newSize);
    if (newSize == 0) return nullptr;
    auto* header = reinterpret_cast<BlockHeader*>(
        static_cast<uint8_t*>(pointer) - sizeof(BlockHeader));
    if (newSize <= header->size) return pointer;
    void* replacement = allocate(newSize);
    if (!replacement) return nullptr;
    memcpy(replacement, pointer, header->size);
    return replacement;
  }

 private:
  struct alignas(std::max_align_t) BlockHeader {
    size_t size;
  };

  static constexpr size_t ALIGNMENT = alignof(BlockHeader);

  static constexpr size_t alignUp(size_t value) {
    return (value + ALIGNMENT - 1U) & ~(ALIGNMENT - 1U);
  }

  alignas(std::max_align_t) uint8_t storage_[Capacity] = {};
  size_t used_ = 0;
};
