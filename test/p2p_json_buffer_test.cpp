#include "P2PJsonBuffer.h"

#include <ArduinoJson.h>

#include <assert.h>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr size_t DOCUMENT_BYTES = 24U * 1024U;
constexpr size_t FILTER_BYTES = 8U * 1024U;

std::vector<char> large551Payload() {
  // Regression shape for the 16,887-byte P2PQuake 551
  // 6ab2223be88ee598246bf344 (216 observation points). Fields outside the
  // production filter intentionally make this payload the same scale as the
  // original message without increasing the filtered document's footprint.
  std::string json =
      R"({"code":551,"id":"6ab2223be88ee598246bf344","time":"2026/09/19 12:00:00","earthquake":{"time":"2026/09/19 11:59:00","maxScale":30,"hypocenter":{"name":"熊本県熊本地方","magnitude":3.5}},"issue":{"time":"2026/09/19 12:00:00","type":"DetailScale","correct":"None"},"points":[)";
  for (size_t index = 0; index < 216; ++index) {
    if (index != 0) json += ',';
    json += R"({"addr":"回帰試験用の観測地点データ","isArea":false,"pref":"熊本県","scale":20})";
  }
  json += "]}";
  while (json.size() < 16887) json += ' ';
  return std::vector<char>(json.begin(), json.end());
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
  const DeserializationError error = deserializeJson(
      document, payload.data(), payload.size(),
      DeserializationOption::Filter(filter));
  assert(!error);
  assert(!document.overflowed());
  assert((document["code"] | 0) == 551);
  const JsonArray points = document["points"].as<JsonArray>();
  assert(points.size() == 216);
  assert(strcmp(points[215]["pref"] | "", "熊本県") == 0);
  assert((points[215]["scale"] | 0) == 20);
}
