#include "WeatherWarningService.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {
constexpr char FEED_URL[] =
    "https://www.data.jma.go.jp/developer/xml/feed/extra.xml";
constexpr char WARNING_TITLE[] = "気象特別警報・警報・注意報";
constexpr char WARNING_TYPE[] = "気象警報・注意報（府県予報区等）";

String tagValue(const String& text, const char* tag, size_t from = 0) {
  const String open = String("<") + tag + ">";
  const String close = String("</") + tag + ">";
  const int start = text.indexOf(open, from);
  if (start < 0) return String();
  const int valueStart = start + open.length();
  const int end = text.indexOf(close, valueStart);
  return end < 0 ? String() : text.substring(valueStart, end);
}

String attributeValue(const String& tag, const char* attribute) {
  const String prefix = String(attribute) + "=\"";
  const int start = tag.indexOf(prefix);
  if (start < 0) return String();
  const int valueStart = start + prefix.length();
  const int end = tag.indexOf('"', valueStart);
  return end < 0 ? String() : tag.substring(valueStart, end);
}

bool hasName(const String& name, const char* token) {
  return name.indexOf(token) >= 0;
}
}  // namespace

void WeatherWarningService::begin(const char* const* targetCodes,
                                  const char* const* targetNames,
                                  size_t targetCount) {
  targetCount_ = 0;
  const size_t requested = min(targetCount, MAX_TARGETS);
  for (size_t index = 0; index < requested; ++index) {
    if (!targetCodes[index] || !targetNames[index] ||
        targetCodes[index][0] == '\0' || targetNames[index][0] == '\0') {
      continue;
    }
    strlcpy(targets_[targetCount_].code, targetCodes[index],
            sizeof(targets_[targetCount_].code));
    strlcpy(targets_[targetCount_].name, targetNames[index],
            sizeof(targets_[targetCount_].name));
    ++targetCount_;
  }
  status_ = targetCount_ == 0 ? Status::NotConfigured : Status::NotAttempted;
  Serial.printf("Weather warning service ready (%u target area(s)).\n",
                static_cast<unsigned>(targetCount_));
}

const WeatherWarningService::Warning* WeatherWarningService::warning(
    size_t index) const {
  return index < warningCount_ ? &warnings_[index] : nullptr;
}

const WeatherWarningService::Warning* WeatherWarningService::highestPriority()
    const {
  if (warningCount_ == 0) return nullptr;
  for (size_t index = 0; index < warningCount_; ++index) {
    if (warnings_[index].special) return &warnings_[index];
  }
  return &warnings_[0];
}

const char* WeatherWarningService::statusText() const {
  switch (status_) {
    case Status::Available: return "Updated";
    case Status::NotConfigured: return "No target areas";
    case Status::WiFiUnavailable: return "Wi-Fi unavailable";
    case Status::FeedFailed: return "Atom feed failed";
    case Status::XmlFailed: return "Warning XML failed";
    case Status::ParseFailed: return "Warning XML invalid";
    default: return "Not updated";
  }
}

bool WeatherWarningService::isSeen(const char* id) const {
  if (!id || id[0] == '\0') return true;
  for (size_t index = 0; index < seenCount_; ++index) {
    if (strcmp(seenIds_[index], id) == 0) return true;
  }
  return false;
}

void WeatherWarningService::rememberId(const char* id) {
  if (!id || id[0] == '\0' || isSeen(id)) return;
  if (seenCount_ == MAX_SEEN_IDS) {
    for (size_t index = 1; index < seenCount_; ++index) {
      strlcpy(seenIds_[index - 1], seenIds_[index], sizeof(seenIds_[0]));
    }
    --seenCount_;
  }
  strlcpy(seenIds_[seenCount_++], id, sizeof(seenIds_[0]));
}

bool WeatherWarningService::parseEntry(const String& entry, String* id,
                                       String* url) const {
  const String title = tagValue(entry, "title");
  if (title != WARNING_TITLE) return false;
  *id = tagValue(entry, "id");
  const int linkStart = entry.indexOf("<link");
  const int linkEnd = linkStart < 0 ? -1 : entry.indexOf('>', linkStart);
  if (id->isEmpty() || linkEnd < 0) return false;
  *url = attributeValue(entry.substring(linkStart, linkEnd + 1), "href");
  return !url->isEmpty();
}

int WeatherWarningService::targetIndexForCode(const char* code) const {
  for (size_t index = 0; index < targetCount_; ++index) {
    if (strcmp(targets_[index].code, code) == 0) return index;
  }
  return -1;
}

int WeatherWarningService::targetIndexForUrl(const String& url) const {
  for (size_t index = 0; index < targetCount_; ++index) {
    const String suffix = String("_") + targets_[index].code + ".xml";
    if (url.endsWith(suffix)) return static_cast<int>(index);
  }
  return -1;
}

void WeatherWarningService::clearTargetWarnings(size_t targetIndex) {
  const char* targetName = targets_[targetIndex].name;
  size_t write = 0;
  for (size_t index = 0; index < warningCount_; ++index) {
    if (strcmp(warnings_[index].area, targetName) == 0) continue;
    if (write != index) warnings_[write] = warnings_[index];
    ++write;
  }
  warningCount_ = write;
}

void WeatherWarningService::addWarning(const Target& target, const char* name,
                                       bool special, bool continued) {
  if (warningCount_ >= MAX_WARNINGS) {
    Serial.println("Weather warning list full; warning ignored.");
    return;
  }
  Warning& warning = warnings_[warningCount_++];
  strlcpy(warning.area, target.name, sizeof(warning.area));
  strlcpy(warning.name, name, sizeof(warning.name));
  warning.special = special;
  warning.continued = continued;
}

bool WeatherWarningService::applyWarningItem(const String& item) {
  const int areaStart = item.indexOf("<Area>");
  const int areaEnd = areaStart < 0 ? -1 : item.indexOf("</Area>", areaStart);
  if (areaEnd < 0) return false;
  const String area = item.substring(areaStart, areaEnd);
  const String code = tagValue(area, "Code");
  const int targetIndex = targetIndexForCode(code.c_str());
  if (targetIndex < 0) return false;

  clearTargetWarnings(static_cast<size_t>(targetIndex));
  int kindStart = item.indexOf("<Kind>");
  while (kindStart >= 0) {
    const int kindEnd = item.indexOf("</Kind>", kindStart);
    if (kindEnd < 0) break;
    const String kind = item.substring(kindStart, kindEnd);
    const String name = tagValue(kind, "Name");
    const String status = tagValue(kind, "Status");
    const bool special = hasName(name, "特別警報");
    const bool warning = !special && hasName(name, "警報") &&
                         !hasName(name, "注意報");
    if ((special || warning) && status != "解除") {
      addWarning(targets_[targetIndex], name.c_str(), special,
                 status == "継続");
    }
    kindStart = item.indexOf("<Kind>", kindEnd + 7);
  }
  return true;
}

bool WeatherWarningService::applyWarningXml(const String& url) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) return false;
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("JMA warning XML returned HTTP %d.\n", status);
    http.end();
    return false;
  }

  Stream& stream = http.getStream();
  bool matchedSection = false;
  bool inSection = false;
  bool inItem = false;
  String item;
  item.reserve(4096);
  while (http.connected() || stream.available()) {
    String line = stream.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;
    if (!inSection) {
      if (line.startsWith("<Information") &&
          line.indexOf(WARNING_TYPE) >= 0) {
        inSection = true;
        matchedSection = true;
      }
      continue;
    }
    if (line.startsWith("</Information>")) break;
    if (line.startsWith("<Item>")) {
      inItem = true;
      item = "";
    }
    if (!inItem) continue;
    if (item.length() + line.length() > 4096) {
      Serial.println("JMA warning item exceeded parser limit.");
      http.end();
      return false;
    }
    item += line;
    if (line.indexOf("</Item>") >= 0) {
      // Items for other forecast areas are expected in this XML.
      (void)applyWarningItem(item);
      inItem = false;
    }
  }
  http.end();
  return matchedSection;
}

bool WeatherWarningService::poll() {
  if (targetCount_ == 0) {
    status_ = Status::NotConfigured;
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    status_ = Status::WiFiUnavailable;
    return false;
  }
  Serial.println("Requesting JMA weather warning Atom feed...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, FEED_URL)) {
    status_ = Status::FeedFailed;
    return false;
  }
  const int response = http.GET();
  if (response != HTTP_CODE_OK) {
    Serial.printf("JMA Atom feed returned HTTP %d.\n", response);
    http.end();
    status_ = Status::FeedFailed;
    return false;
  }

  struct PendingEntry {
    String id;
    String url;
  } pending[MAX_TARGETS];
  bool selected[MAX_TARGETS] = {};
  size_t selectedCount = 0;
  Stream& stream = http.getStream();
  bool inEntry = false;
  String entry;
  entry.reserve(2048);
  while (http.connected() || stream.available()) {
    String line = stream.readStringUntil('\n');
    line.trim();
    if (line.isEmpty()) continue;
    if (line.startsWith("<entry>")) {
      inEntry = true;
      entry = line;
      continue;
    }
    if (!inEntry) continue;
    if (entry.length() + line.length() > 2048) {
      Serial.println("JMA Atom entry exceeded parser limit.");
      http.end();
      status_ = Status::FeedFailed;
      return false;
    }
    entry += line;
    if (line.indexOf("</entry>") < 0) continue;

    String id, url;
    if (parseEntry(entry, &id, &url) && !isSeen(id.c_str())) {
      const int targetIndex = targetIndexForUrl(url);
      if (targetIndex >= 0 && !selected[targetIndex]) {
        pending[targetIndex].id = id;
        pending[targetIndex].url = url;
        selected[targetIndex] = true;
        ++selectedCount;
      }
    }
    inEntry = false;
    if (selectedCount == targetCount_) break;
  }
  http.end();

  bool parsedAny = false;
  for (size_t index = 0; index < targetCount_; ++index) {
    if (!selected[index]) continue;
    if (!applyWarningXml(pending[index].url)) {
      status_ = Status::XmlFailed;
      return false;
    }
    rememberId(pending[index].id.c_str());
    parsedAny = true;
  }
  status_ = Status::Available;
  Serial.printf("Weather warning update: %u active warning(s), %s new XML.\n",
                static_cast<unsigned>(warningCount_), parsedAny ? "with" : "no");
  return true;
}
