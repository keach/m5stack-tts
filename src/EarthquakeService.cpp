#include "EarthquakeService.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include "RuntimeDiagnostics.h"

namespace {
constexpr char PRODUCTION_HOST[] = "api.p2pquake.net";
constexpr char SANDBOX_HOST[] = "api-realtime-sandbox.p2pquake.net";
constexpr char WEB_SOCKET_PATH[] = "/v2/ws";
constexpr uint16_t WEB_SOCKET_PORT = 443;
constexpr unsigned long EEW_DISPLAY_MS = 2UL * 60UL * 1000UL;
constexpr unsigned long EEW_CANCEL_DISPLAY_MS = 30UL * 1000UL;
constexpr unsigned long EARTHQUAKE_DISPLAY_MS = 60UL * 1000UL;
constexpr unsigned long CONNECT_TIMEOUT_MS = 15UL * 1000UL;
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
constexpr time_t MAX_EVENT_AGE_SECONDS = 5 * 60;
constexpr unsigned long RECONNECT_DELAYS_MS[] = {1000, 2000, 5000, 10000,
                                                  30000};

struct EewAreaMapping {
  const char* area;
  const char* prefecture;
};

// P2PQuake EEW areas use JMA forecast-region names rather than consistently
// using prefecture names. Exact prefecture names still match without this map.
constexpr EewAreaMapping EEW_AREA_MAPPINGS[] = {
    {"北海道", "北海道"}, {"青森", "青森県"}, {"岩手", "岩手県"},
    {"宮城", "宮城県"}, {"秋田", "秋田県"}, {"山形", "山形県"},
    {"福島", "福島県"}, {"茨城", "茨城県"}, {"栃木", "栃木県"},
    {"群馬", "群馬県"}, {"埼玉", "埼玉県"}, {"千葉", "千葉県"},
    {"東京", "東京都"}, {"神奈川", "神奈川県"}, {"新潟", "新潟県"},
    {"富山", "富山県"}, {"石川", "石川県"}, {"福井", "福井県"},
    {"山梨", "山梨県"}, {"長野", "長野県"}, {"岐阜", "岐阜県"},
    {"静岡", "静岡県"}, {"愛知", "愛知県"}, {"三重", "三重県"},
    {"滋賀", "滋賀県"}, {"京都", "京都府"}, {"大阪", "大阪府"},
    {"兵庫", "兵庫県"}, {"奈良", "奈良県"}, {"和歌山", "和歌山県"},
    {"鳥取", "鳥取県"}, {"島根", "島根県"}, {"岡山", "岡山県"},
    {"広島", "広島県"}, {"山口", "山口県"}, {"徳島", "徳島県"},
    {"香川", "香川県"}, {"愛媛", "愛媛県"}, {"高知", "高知県"},
    {"福岡", "福岡県"}, {"佐賀", "佐賀県"}, {"長崎", "長崎県"},
    {"熊本", "熊本県"}, {"大分", "大分県"}, {"宮崎", "宮崎県"},
    {"鹿児島", "鹿児島県"}, {"沖縄", "沖縄県"},
    {"北海道道央", "北海道"}, {"北海道道南", "北海道"},
    {"北海道道北", "北海道"}, {"北海道道東", "北海道"},
    {"石狩地方", "北海道"},   {"空知地方", "北海道"},
    {"後志地方", "北海道"},   {"渡島地方", "北海道"},
    {"檜山地方", "北海道"},   {"胆振地方", "北海道"},
    {"日高地方", "北海道"},   {"上川地方", "北海道"},
    {"留萌地方", "北海道"},   {"宗谷地方", "北海道"},
    {"網走地方", "北海道"},   {"北見地方", "北海道"},
    {"紋別地方", "北海道"},   {"十勝地方", "北海道"},
    {"釧路地方", "北海道"},   {"根室地方", "北海道"},
    {"北海道奥尻島", "北海道"},
    {"奄美群島", "鹿児島県"}, {"鹿児島県（奄美除く）", "鹿児島県"},
    {"沖縄本島地方", "沖縄県"}, {"大東島地方", "沖縄県"},
    {"宮古島地方", "沖縄県"}, {"八重山地方", "沖縄県"},
};

constexpr const char* VALID_PREFECTURES[] = {
    "北海道",   "青森県", "岩手県",   "宮城県", "秋田県", "山形県",
    "福島県",   "茨城県", "栃木県",   "群馬県", "埼玉県", "千葉県",
    "東京都",   "神奈川県", "新潟県", "富山県", "石川県", "福井県",
    "山梨県",   "長野県", "岐阜県",   "静岡県", "愛知県", "三重県",
    "滋賀県",   "京都府", "大阪府",   "兵庫県", "奈良県", "和歌山県",
    "鳥取県",   "島根県", "岡山県",   "広島県", "山口県", "徳島県",
    "香川県",   "愛媛県", "高知県",   "福岡県", "佐賀県", "長崎県",
    "熊本県",   "大分県", "宮崎県",   "鹿児島県", "沖縄県",
};

void copyText(char* destination, size_t capacity, const char* source) {
  if (!destination || capacity == 0) return;
  strlcpy(destination, source ? source : "", capacity);
}

const char* messageId(JsonDocument& document) {
  const char* id = document["id"] | "";
  return id[0] != '\0' ? id : document["_id"] | "";
}

const char* scaleText(int scale) {
  switch (scale) {
    case 0: return "0";
    case 10: return "1";
    case 20: return "2";
    case 30: return "3";
    case 40: return "4";
    case 45: return "5弱";
    case 46: return "5弱以上";
    case 50: return "5強";
    case 55: return "6弱";
    case 60: return "6強";
    case 70: return "7";
    case 99: return "5弱以上";
    default: return "不明";
  }
}

void appendArea(char* destination, size_t capacity, const char* area,
                size_t matchedCount) {
  if (matchedCount > 2 || !area || area[0] == '\0') return;
  if (destination[0] != '\0') strlcat(destination, "・", capacity);
  strlcat(destination, area, capacity);
}

bool parseJstTime(const char* value, time_t* result) {
  if (!value || !result || strlen(value) < 19) return false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (sscanf(value, "%d/%d/%d %d:%d:%d", &year, &month, &day, &hour,
             &minute, &second) != 6) {
    return false;
  }
  // Convert a civil date to Unix days without relying on timegm(), which is
  // not available in the ESP32 Arduino toolchain.
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
  const unsigned dayOfYear =
      (153U * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2U) /
          5U +
      static_cast<unsigned>(day - 1);
  const unsigned dayOfEra =
      yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
  const int64_t days = era * 146097LL + static_cast<int64_t>(dayOfEra) -
                       719468LL;
  *result = static_cast<time_t>(days * 86400LL + hour * 3600 + minute * 60 +
                                second - 9 * 60 * 60);
  return *result >= MINIMUM_VALID_TIME;
}

void copyDisplayTime(char* destination, size_t capacity, const char* source) {
  if (!source || strlen(source) < 16) {
    copyText(destination, capacity, "--:--");
    return;
  }
  char value[6] = {source[11], source[12], source[13], source[14],
                   source[15], '\0'};
  copyText(destination, capacity, value);
}
}  // namespace

EarthquakeService* EarthquakeService::instance_ = nullptr;

void EarthquakeService::begin(const char* const* targetPrefectures,
                              size_t targetCount, bool useSandbox,
                              bool allowSandboxAudio) {
  const size_t requestedCount = min(targetCount, MAX_TARGET_PREFECTURES);
  if (targetCount > MAX_TARGET_PREFECTURES) {
    Serial.printf("Earthquake target list truncated from %u to %u entries.\n",
                  static_cast<unsigned>(targetCount),
                  static_cast<unsigned>(MAX_TARGET_PREFECTURES));
  }
  for (size_t index = 0; index < requestedCount; ++index) {
    if (!targetPrefectures[index] || targetPrefectures[index][0] == '\0') {
      continue;
    }
    bool valid = false;
    for (const char* prefecture : VALID_PREFECTURES) {
      if (strcmp(prefecture, targetPrefectures[index]) == 0) {
        valid = true;
        break;
      }
    }
    if (!valid) {
      Serial.printf("Invalid earthquake target prefecture ignored: %s\n",
                    targetPrefectures[index]);
      continue;
    }
    bool duplicate = false;
    for (size_t existing = 0; existing < targetCount_; ++existing) {
      if (strcmp(targets_[existing], targetPrefectures[index]) == 0) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      Serial.printf("Duplicate earthquake target prefecture ignored: %s\n",
                    targetPrefectures[index]);
    } else {
      targets_[targetCount_++] = targetPrefectures[index];
    }
  }
  useSandbox_ = useSandbox;
  allowSandboxAudio_ = allowSandboxAudio;
  if (targetCount_ == 0) {
    Serial.println("Earthquake target prefectures are not configured.");
  }

  preferences_.begin("earthquake", false);
  String storedId = preferences_.getString("last_id", "");
  if (!storedId.isEmpty()) rememberId(storedId.c_str());
  String storedEventId = preferences_.getString("eew_event", "");
  copyText(lastEewEventId_, sizeof(lastEewEventId_), storedEventId.c_str());
  lastEewSerial_ = preferences_.getInt("eew_serial", 0);
  String storedWarnedEventId = preferences_.getString("eew_warn", "");
  copyText(lastWarnedEewEventId_, sizeof(lastWarnedEewEventId_),
           storedWarnedEventId.c_str());
  String storedShortEventId = preferences_.getString("eew_short", "");
  copyText(lastShortEewEventId_, sizeof(lastShortEewEventId_),
           storedShortEventId.c_str());
  String storedEarthquakeKey = preferences_.getString("quake_sound", "");
  copyText(lastSoundedEarthquakeKey_, sizeof(lastSoundedEarthquakeKey_),
           storedEarthquakeKey.c_str());

  instance_ = this;
  webSocket_.onEvent(eventThunk);
  webSocket_.setReconnectInterval(0xffffffffUL);
  webSocket_.enableHeartbeat(15000, 5000, 2);
  started_ = true;
  reconnectAt_ = millis();
  Serial.printf("P2PQuake service ready (%s, %u target prefecture(s)).\n",
                useSandbox_ ? "sandbox" : "production",
                static_cast<unsigned>(targetCount_));
}

void EarthquakeService::loop() {
  if (!started_) return;
  if (paused_) {
    expireEvents();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (connected_) webSocket_.disconnect();
    connected_ = false;
    connecting_ = false;
    expireEvents();
    return;
  }
  if (!connected_ && !connecting_ &&
      static_cast<long>(millis() - reconnectAt_) >= 0) {
    connect();
  }
  webSocket_.loop();
  if (connecting_ && !connected_ &&
      millis() - connectStartedAt_ >= CONNECT_TIMEOUT_MS) {
    Serial.println("P2PQuake WebSocket connection timed out.");
    connecting_ = false;
    webSocket_.setReconnectInterval(0xffffffffUL);
    scheduleReconnect();
  }
  expireEvents();
}

bool EarthquakeService::pauseForNetworkRequest() {
  if (!started_ || paused_ || (!connected_ && !connecting_)) return false;
  Serial.println("Pausing P2PQuake WebSocket for HTTPS requests.");
  paused_ = true;
  webSocket_.disconnect();
  connected_ = false;
  connecting_ = false;
  logRuntimeMemory("P2PQuake paused");
  return true;
}

void EarthquakeService::resumeAfterNetworkRequest() {
  if (!started_ || !paused_) return;
  paused_ = false;
  reconnectStep_ = 0;
  reconnectAt_ = millis();
  Serial.println("P2PQuake WebSocket resume requested.");
  logRuntimeMemory("P2PQuake resume requested");
}

bool EarthquakeService::active() const {
  return selectedType_ != SeismicEventType::None;
}

const SeismicEvent& EarthquakeService::current() const {
  if (selectedType_ == SeismicEventType::Eew) return eew_;
  if (selectedType_ == SeismicEventType::Earthquake) return earthquake_;
  return empty_;
}

bool EarthquakeService::consumeDisplayChanged() {
  const bool changed = displayChanged_;
  displayChanged_ = false;
  return changed;
}

SeismicSoundType EarthquakeService::consumeSoundRequested() {
  const SeismicSoundType requested = soundRequested_;
  soundRequested_ = SeismicSoundType::None;
  return requested;
}

bool EarthquakeService::consumeWakeRequested() {
  const bool requested = wakeRequested_;
  wakeRequested_ = false;
  return requested;
}

void EarthquakeService::eventThunk(WStype_t type, uint8_t* payload,
                                   size_t length) {
  if (instance_) instance_->onWebSocketEvent(type, payload, length);
}

void EarthquakeService::onWebSocketEvent(WStype_t type, uint8_t* payload,
                                         size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      connected_ = true;
      connecting_ = false;
      reconnectStep_ = 0;
      Serial.printf("P2PQuake WebSocket connected: %s\n", payload);
      logRuntimeMemory("P2PQuake connected");
      break;
    case WStype_DISCONNECTED:
      if (connected_) Serial.println("P2PQuake WebSocket disconnected.");
      connected_ = false;
      connecting_ = false;
      webSocket_.setReconnectInterval(0xffffffffUL);
      if (!paused_) scheduleReconnect();
      logRuntimeMemory("P2PQuake disconnected");
      break;
    case WStype_TEXT:
      processMessage(payload, length);
      break;
    case WStype_ERROR:
      Serial.println("P2PQuake WebSocket error.");
      logRuntimeMemory("P2PQuake error");
      break;
    default:
      break;
  }
}

void EarthquakeService::processMessage(const uint8_t* payload, size_t length) {
  if (!payload || length == 0 || length > 24576) {
    Serial.printf("P2PQuake message rejected (length=%u).\n",
                  static_cast<unsigned>(length));
    return;
  }
  JsonDocument document;
  JsonDocument filter;
  filter["id"] = true;
  filter["_id"] = true;
  filter["code"] = true;
  filter["time"] = true;
  filter["test"] = true;
  filter["cancelled"] = true;
  filter["issue"]["time"] = true;
  filter["issue"]["eventId"] = true;
  filter["issue"]["serial"] = true;
  filter["earthquake"]["time"] = true;
  filter["earthquake"]["maxScale"] = true;
  filter["earthquake"]["magnitude"] = true;
  filter["earthquake"]["hypocenter"]["name"] = true;
  filter["earthquake"]["hypocenter"]["magnitude"] = true;
  filter["areas"][0]["pref"] = true;
  filter["areas"][0]["scaleFrom"] = true;
  filter["areas"][0]["scaleTo"] = true;
  filter["points"][0]["pref"] = true;
  filter["points"][0]["scale"] = true;
  const DeserializationError error = deserializeJson(
      document, payload, length, DeserializationOption::Filter(filter));
  if (error) {
    Serial.printf("P2PQuake JSON parse failed: %s\n", error.c_str());
    return;
  }
  const int code = document["code"] | 0;
  if (code != 551 && code != 556) return;
  const char* id = messageId(document);
  if (id[0] == '\0' || isDuplicateId(id)) return;
  Serial.printf("P2PQuake message received: code=%d id=%s.\n", code, id);
  if (isStale(document["time"] | "")) {
    Serial.printf("P2PQuake stale message ignored: %s\n", id);
    rememberId(id);
    return;
  }
  rememberId(id);
  if (code == 556) {
    processEew(document);
  } else {
    processEarthquake(document);
  }
}

void EarthquakeService::processEew(JsonDocument& document) {
  const char* eventId = document["issue"]["eventId"] | "";
  const JsonVariantConst serialValue = document["issue"]["serial"];
  const int serial = serialValue.is<const char*>()
                         ? atoi(serialValue.as<const char*>())
                         : serialValue.as<int>();
  if (eventId[0] == '\0' || serial <= 0) return;
  if (strcmp(lastEewEventId_, eventId) == 0 && serial <= lastEewSerial_) return;

  SeismicEvent updated;
  updated.type = SeismicEventType::Eew;
  copyText(updated.id, sizeof(updated.id), messageId(document));
  copyText(updated.eventId, sizeof(updated.eventId), eventId);
  copyText(updated.hypocenter, sizeof(updated.hypocenter),
           document["earthquake"]["hypocenter"]["name"] | "不明");
  copyDisplayTime(updated.eventTime, sizeof(updated.eventTime),
                  document["issue"]["time"] | document["time"] | "");
  updated.serial = serial;
  updated.maxScale = document["earthquake"]["maxScale"] | -1;
  updated.magnitude = document["earthquake"]["hypocenter"]["magnitude"] |
                      document["earthquake"]["magnitude"] | -1.0F;
  updated.cancelled = document["cancelled"] | false;
  updated.test = useSandbox_ || (document["test"] | false);
  updated.receivedAt = millis();

  size_t matchedCount = 0;
  const JsonArray areas = document["areas"].as<JsonArray>();
  for (JsonObject area : areas) {
    const int areaScale = area["scaleTo"] | area["scaleFrom"] | -1;
    if (areaScale > updated.maxScale) updated.maxScale = areaScale;
  }
  for (size_t target = 0; target < targetCount_; ++target) {
    bool targetMatched = false;
    for (JsonObject area : areas) {
      const char* areaName = area["pref"] | "";
      if (!eewAreaMatchesPrefecture(areaName, targets_[target])) continue;
      targetMatched = true;
    }
    if (targetMatched) {
      ++matchedCount;
      appendArea(updated.targetAreas, sizeof(updated.targetAreas),
                 targets_[target], matchedCount);
    }
  }
  updated.targetMatched = matchedCount > 0;
  if (updated.cancelled && areas.size() == 0 &&
      strcmp(eew_.eventId, eventId) == 0) {
    updated.targetMatched = eew_.targetMatched;
    copyText(updated.targetAreas, sizeof(updated.targetAreas),
             eew_.targetAreas);
    updated.maxScale = eew_.maxScale;
    if (strcmp(updated.hypocenter, "不明") == 0) {
      copyText(updated.hypocenter, sizeof(updated.hypocenter),
               eew_.hypocenter);
    }
    if (updated.magnitude < 0) updated.magnitude = eew_.magnitude;
  }
  if (matchedCount > 2) {
    char suffix[24];
    snprintf(suffix, sizeof(suffix), " ほか%u地域",
             static_cast<unsigned>(matchedCount - 2));
    strlcat(updated.targetAreas, suffix, sizeof(updated.targetAreas));
  }
  if (!updated.targetMatched) copyText(updated.targetAreas,
                                       sizeof(updated.targetAreas), "対象外");

  eew_ = updated;
  copyText(lastEewEventId_, sizeof(lastEewEventId_), eventId);
  lastEewSerial_ = serial;
  preferences_.putString("eew_event", eventId);
  preferences_.putInt("eew_serial", serial);
  // Every EEW wakes the display. Target matches use the dedicated warning
  // tone; non-target events use one short tone. A later target match may still
  // play the dedicated tone after an earlier non-target update.
  wakeRequested_ = true;
  const bool audioAllowed =
      !updated.test || (useSandbox_ && allowSandboxAudio_);
  if (audioAllowed && !updated.cancelled) {
    if (updated.targetMatched &&
        strcmp(lastWarnedEewEventId_, eventId) != 0) {
      soundRequested_ = SeismicSoundType::EewWarning;
      copyText(lastWarnedEewEventId_, sizeof(lastWarnedEewEventId_), eventId);
      preferences_.putString("eew_warn", eventId);
    } else if (!updated.targetMatched &&
               strcmp(lastShortEewEventId_, eventId) != 0) {
      if (soundRequested_ == SeismicSoundType::None) {
        soundRequested_ = SeismicSoundType::Short;
      }
      copyText(lastShortEewEventId_, sizeof(lastShortEewEventId_), eventId);
      preferences_.putString("eew_short", eventId);
    }
  }
  Serial.printf("EEW received: event=%s serial=%d scale=%s target=%s%s%s\n",
                updated.eventId, updated.serial, scaleText(updated.maxScale),
                updated.targetMatched ? "yes" : "no",
                updated.cancelled ? " cancelled" : "",
                updated.test ? " test" : "");
  selectCurrentEvent();
}

void EarthquakeService::processEarthquake(JsonDocument& document) {
  SeismicEvent updated;
  updated.type = SeismicEventType::Earthquake;
  copyText(updated.id, sizeof(updated.id), messageId(document));
  copyText(updated.hypocenter, sizeof(updated.hypocenter),
           document["earthquake"]["hypocenter"]["name"] | "不明");
  copyDisplayTime(updated.eventTime, sizeof(updated.eventTime),
                  document["earthquake"]["time"] | document["time"] | "");
  updated.magnitude = document["earthquake"]["hypocenter"]["magnitude"] |
                      document["earthquake"]["magnitude"] | -1.0F;
  updated.test = useSandbox_;
  updated.receivedAt = millis();
  const int overallMaxScale = document["earthquake"]["maxScale"] | -1;

  size_t matchedCount = 0;
  const JsonArray points = document["points"].as<JsonArray>();
  for (size_t target = 0; target < targetCount_; ++target) {
    bool targetMatched = false;
    for (JsonObject point : points) {
      if (strcmp(point["pref"] | "", targets_[target]) != 0) continue;
      targetMatched = true;
      const int scale = point["scale"] | -1;
      if (scale > updated.maxScale) updated.maxScale = scale;
    }
    if (targetMatched) {
      ++matchedCount;
      appendArea(updated.targetAreas, sizeof(updated.targetAreas),
                 targets_[target], matchedCount);
    }
  }
  if (matchedCount > 2) {
    char suffix[24];
    snprintf(suffix, sizeof(suffix), " ほか%u地域",
             static_cast<unsigned>(matchedCount - 2));
    strlcat(updated.targetAreas, suffix, sizeof(updated.targetAreas));
  }
  updated.targetMatched = matchedCount > 0;
  if (!updated.targetMatched) {
    updated.maxScale = overallMaxScale;
    copyText(updated.targetAreas, sizeof(updated.targetAreas), "対象外");
  }
  earthquake_ = updated;
  if (updated.targetMatched) {
    wakeRequested_ = true;
    const bool audioAllowed =
        !updated.test || (useSandbox_ && allowSandboxAudio_);
    char logicalKey[sizeof(lastSoundedEarthquakeKey_)] = {};
    snprintf(logicalKey, sizeof(logicalKey), "%s|%s",
             document["earthquake"]["time"] | document["time"] | "",
             updated.hypocenter);
    if (audioAllowed && strcmp(lastSoundedEarthquakeKey_, logicalKey) != 0) {
      if (soundRequested_ == SeismicSoundType::None) {
        soundRequested_ = SeismicSoundType::Short;
      }
      copyText(lastSoundedEarthquakeKey_, sizeof(lastSoundedEarthquakeKey_),
               logicalKey);
      preferences_.putString("quake_sound", logicalKey);
    }
  }
  Serial.printf("Earthquake information received: scale=%s areas=%s%s\n",
                scaleText(updated.maxScale), updated.targetAreas,
                updated.test ? " test" : "");
  selectCurrentEvent();
}

bool EarthquakeService::isDuplicateId(const char* id) const {
  for (size_t index = 0; index < recentIdCount_; ++index) {
    if (strcmp(recentIds_[index], id) == 0) return true;
  }
  return false;
}

void EarthquakeService::rememberId(const char* id) {
  if (!id || id[0] == '\0' || isDuplicateId(id)) return;
  copyText(recentIds_[nextRecentId_], sizeof(recentIds_[nextRecentId_]), id);
  nextRecentId_ = (nextRecentId_ + 1) % RECENT_ID_COUNT;
  recentIdCount_ = min(recentIdCount_ + 1, RECENT_ID_COUNT);
  preferences_.putString("last_id", id);
}

bool EarthquakeService::isTargetPrefecture(const char* prefecture) const {
  if (!prefecture || prefecture[0] == '\0') return false;
  for (size_t index = 0; index < targetCount_; ++index) {
    if (targets_[index] && strcmp(targets_[index], prefecture) == 0) return true;
  }
  return false;
}

bool EarthquakeService::eewAreaMatchesPrefecture(
    const char* area, const char* prefecture) const {
  if (!area || !prefecture) return false;
  if (strcmp(area, prefecture) == 0) return true;
  for (const EewAreaMapping& mapping : EEW_AREA_MAPPINGS) {
    if (strcmp(mapping.area, area) == 0 &&
        strcmp(mapping.prefecture, prefecture) == 0) {
      return true;
    }
  }
  return false;
}

bool EarthquakeService::isStale(const char* basicTime) const {
  if (useSandbox_) return false;
  time_t messageTime = 0;
  time_t now = time(nullptr);
  return now >= MINIMUM_VALID_TIME && parseJstTime(basicTime, &messageTime) &&
         now - messageTime > MAX_EVENT_AGE_SECONDS;
}

void EarthquakeService::selectCurrentEvent() {
  const SeismicEventType previous = selectedType_;
  selectedType_ = eew_.type == SeismicEventType::Eew
                      ? SeismicEventType::Eew
                      : earthquake_.type == SeismicEventType::Earthquake
                            ? SeismicEventType::Earthquake
                            : SeismicEventType::None;
  displayChanged_ = true;
  if (previous != selectedType_) {
    Serial.printf("Seismic display changed: %u -> %u.\n",
                  static_cast<unsigned>(previous),
                  static_cast<unsigned>(selectedType_));
  }
}

void EarthquakeService::expireEvents() {
  const unsigned long now = millis();
  bool expired = false;
  if (eew_.type == SeismicEventType::Eew) {
    const unsigned long duration =
        eew_.cancelled ? EEW_CANCEL_DISPLAY_MS : EEW_DISPLAY_MS;
    if (now - eew_.receivedAt >= duration) {
      eew_ = SeismicEvent();
      expired = true;
    }
  }
  if (earthquake_.type == SeismicEventType::Earthquake &&
      now - earthquake_.receivedAt >= EARTHQUAKE_DISPLAY_MS) {
    earthquake_ = SeismicEvent();
    expired = true;
  }
  if (expired) selectCurrentEvent();
}

void EarthquakeService::scheduleReconnect() {
  const size_t delayIndex =
      min(static_cast<size_t>(reconnectStep_),
          sizeof(RECONNECT_DELAYS_MS) / sizeof(RECONNECT_DELAYS_MS[0]) - 1);
  const unsigned long delayMs = RECONNECT_DELAYS_MS[delayIndex];
  reconnectAt_ = millis() + delayMs;
  if (reconnectStep_ < 4) ++reconnectStep_;
  Serial.printf("P2PQuake reconnect scheduled in %u ms.\n",
                static_cast<unsigned>(delayMs));
}

void EarthquakeService::connect() {
  const char* host = useSandbox_ ? SANDBOX_HOST : PRODUCTION_HOST;
  Serial.printf("Connecting to P2PQuake WebSocket: wss://%s%s\n", host,
                WEB_SOCKET_PATH);
  logRuntimeMemory("P2PQuake before beginSSL");
  connecting_ = true;
  connectStartedAt_ = millis();
  webSocket_.beginSSL(host, WEB_SOCKET_PORT, WEB_SOCKET_PATH);
  webSocket_.setReconnectInterval(0);
  logRuntimeMemory("P2PQuake after beginSSL");
}
