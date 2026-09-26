#include "P2PJsonBuffer.h"

#include <ArduinoJson.h>

#include <assert.h>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
constexpr size_t DOCUMENT_BYTES = 24U * 1024U;
constexpr size_t FILTER_BYTES = 8U * 1024U;

std::vector<char> large551Payload() {
  // P2PQuake 551 6ab2223be88ee598246bf344: 16,887 bytes and 216
  // observation points. The committed fixture keeps this regression test
  // independent from the network.
  std::ifstream file("test/fixtures/p2p_551_large.json", std::ios::binary);
  assert(file.good());
  return std::vector<char>(std::istreambuf_iterator<char>(file), {});
}

void buildFilter(JsonDocument& filter) {
  filter["id"] = true;
  filter["_id"] = true;
  filter["code"] = true;
  filter["time"] = true;
  filter["test"] = true;
  filter["cancelled"] = true;
  filter["issue"]["time"] = true;
  filter["issue"]["eventId"] = true;
  filter["issue"]["serial"] = true;
  filter["issue"]["type"] = true;
  filter["issue"]["correct"] = true;
  filter["earthquake"]["time"] = true;
  filter["earthquake"]["originTime"] = true;
  filter["earthquake"]["maxScale"] = true;
  filter["earthquake"]["magnitude"] = true;
  filter["earthquake"]["hypocenter"]["name"] = true;
  filter["earthquake"]["hypocenter"]["magnitude"] = true;
  filter["areas"][0]["pref"] = true;
  filter["areas"][0]["scaleFrom"] = true;
  filter["areas"][0]["scaleTo"] = true;
  filter["points"][0]["pref"] = true;
  filter["points"][0]["scale"] = true;
}
}  // namespace

int main() {
  FixedJsonArenaAllocator<DOCUMENT_BYTES> documentAllocator;
  FixedJsonArenaAllocator<FILTER_BYTES> filterAllocator;
  JsonDocument document(&documentAllocator);
  JsonDocument filter(&filterAllocator);
  buildFilter(filter);
  assert(!filter.overflowed());

  std::vector<char> payload = large551Payload();
  assert(payload.size() == 16888);
  const DeserializationError error = deserializeJson(
      document, payload.data(), payload.size(),
      DeserializationOption::Filter(filter));
  assert(!error);
  assert(!document.overflowed());
  assert((document["code"] | 0) == 551);
  const JsonArray points = document["points"].as<JsonArray>();
  assert(points.size() == 216);
  assert(strcmp(points[215]["pref"] | "", "神奈川県") == 0);
  assert((points[215]["scale"] | 0) == 10);
}
