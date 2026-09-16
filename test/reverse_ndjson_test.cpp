#include "ReverseNdjsonLine.h"

#include <assert.h>
#include <string>
#include <vector>

template <size_t Capacity>
std::vector<std::string> read(const std::string& data) {
  ReverseNdjsonLine<Capacity> reader;
  reader.reset(!data.empty() && data.back() == '\n');
  std::vector<std::string> lines;
  char output[Capacity];
  for (auto byte = data.rbegin(); byte != data.rend(); ++byte) {
    if (reader.consume(*byte, output)) lines.emplace_back(output);
  }
  if (reader.finish(output)) lines.emplace_back(output);
  return lines;
}

int main() {
  assert((read<64>("{\"id\":1}\n{\"id\":2}\n") ==
          std::vector<std::string>{"{\"id\":2}", "{\"id\":1}"}));
  // Syntactically valid JSON without its final newline is still incomplete.
  assert((read<64>("{\"id\":1}\n{\"id\":2}") ==
          std::vector<std::string>{"{\"id\":1}"}));
  assert(read<64>("{\"id\":1}").empty());
  assert(read<64>("").empty());
  assert((read<64>("\nfirst\n\nlast\n") ==
          std::vector<std::string>{"last", "first"}));
  assert((read<8>("ok\n1234567890123456\nnew\n") ==
          std::vector<std::string>{"new", "ok"}));
  assert((read<8>("ok\n1234567890123456") ==
          std::vector<std::string>{"ok"}));
}
