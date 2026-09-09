#include "ThingSpeakPublisher.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "SdCardLock.h"
#include "thingspeak_secrets.h"

namespace {
constexpr char THINGSPEAK_UPDATE_URL[] =
    "https://api.thingspeak.com/update.json";
constexpr char QUEUE_PATH[] = "/thingspeak_queue.ndjson";
constexpr char QUEUE_TEMP_PATH[] = "/thingspeak_queue.tmp";
constexpr char QUEUE_BACKUP_PATH[] = "/thingspeak_queue.bak";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
constexpr uint16_t REQUEST_TIMEOUT_MS = 10000;
constexpr unsigned long MINIMUM_REQUEST_INTERVAL_MS = 15000;
constexpr unsigned long PENDING_QUEUE_RETRY_INTERVAL_MS = 5000;
constexpr size_t MAX_BATCH_RECORDS = 10;

enum class QueueAppendResult { Added, Duplicate, Failed };

bool credentialsAreSet() {
  return THINGSPEAK_CHANNEL_ID != 0 && THINGSPEAK_WRITE_API_KEY[0] != '\0';
}

void formatCreatedAt(time_t observedAt, char* output, size_t outputSize) {
  tm utcTime = {};
  gmtime_r(&observedAt, &utcTime);
  strftime(output, outputSize, "%Y-%m-%dT%H:%M:%SZ", &utcTime);
}

void buildRecord(JsonDocument& record, time_t observedAt, float temperature,
                 int humidity, int pressure, int weatherConditionId,
                 uint8_t precipitationProbability,
                 int temperatureAlertThreshold, int wifiRssi,
                 bool rainAlertActive) {
  char createdAt[21];
  formatCreatedAt(observedAt, createdAt, sizeof(createdAt));
  record["created_at"] = createdAt;
  record["field1"] = temperature;
  record["field2"] = humidity;
  record["field3"] = pressure;
  record["field4"] = weatherConditionId;
  record["field5"] = precipitationProbability;
  record["field6"] = temperatureAlertThreshold;
  record["field7"] = wifiRssi;
  record["field8"] = rainAlertActive ? 1 : 0;
}

bool recoverQueueFiles() {
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) {
    Serial.println("SD card is busy; ThingSpeak queue operation was skipped.");
    return false;
  }
  if (!SD.exists(QUEUE_PATH) && SD.exists(QUEUE_BACKUP_PATH)) {
    if (!SD.rename(QUEUE_BACKUP_PATH, QUEUE_PATH)) {
      Serial.println("Failed to recover the ThingSpeak retry queue.");
      return false;
    }
  } else if (SD.exists(QUEUE_PATH) && SD.exists(QUEUE_BACKUP_PATH)) {
    SD.remove(QUEUE_BACKUP_PATH);
  }
  if (SD.exists(QUEUE_TEMP_PATH)) {
    SD.remove(QUEUE_TEMP_PATH);
  }
  return true;
}

bool queueHasRecords() {
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) {
    Serial.println("SD card is busy; ThingSpeak queue operation was skipped.");
    return false;
  }
  if (!SD.exists(QUEUE_PATH)) return false;
  File file = SD.open(QUEUE_PATH, FILE_READ);
  if (!file) return false;
  const bool hasRecords = file.size() > 0;
  file.close();
  return hasRecords;
}

QueueAppendResult appendToQueue(JsonDocument& record) {
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) {
    Serial.println("SD card is busy; ThingSpeak queue operation was skipped.");
    return QueueAppendResult::Failed;
  }
  const char* createdAt = record["created_at"] | "";
  if (SD.exists(QUEUE_PATH)) {
    File existing = SD.open(QUEUE_PATH, FILE_READ);
    if (!existing) {
      Serial.println("Failed to inspect the ThingSpeak retry queue.");
      return QueueAppendResult::Failed;
    }
    while (existing.available()) {
      String line = existing.readStringUntil('\n');
      line.trim();
      if (line.isEmpty()) continue;
      JsonDocument queuedRecord;
      if (!deserializeJson(queuedRecord, line)) {
        const char* queuedCreatedAt = queuedRecord["created_at"] | "";
        if (strcmp(createdAt, queuedCreatedAt) == 0) {
          existing.close();
          Serial.printf(
              "ThingSpeak retry queue already contains created_at=%s; "
              "duplicate skipped.\n",
              createdAt);
          return QueueAppendResult::Duplicate;
        }
      }
    }
    existing.close();
  }
  File file = SD.open(QUEUE_PATH, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open the ThingSpeak retry queue.");
    return QueueAppendResult::Failed;
  }
  const size_t written = serializeJson(record, file);
  file.println();
  file.close();
  if (written == 0) {
    Serial.println("Failed to append data to the ThingSpeak retry queue.");
    return QueueAppendResult::Failed;
  }
  Serial.printf("Weather data saved to the ThingSpeak retry queue: %s.\n",
                createdAt);
  return QueueAppendResult::Added;
}

bool discardQueuedLines(size_t lineCount) {
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) {
    Serial.println("SD card is busy; ThingSpeak queue operation was skipped.");
    return false;
  }
  File source = SD.open(QUEUE_PATH, FILE_READ);
  if (!source) return false;
  for (size_t index = 0; index < lineCount && source.available(); ++index) {
    source.readStringUntil('\n');
  }
  File temporary = SD.open(QUEUE_TEMP_PATH, FILE_WRITE);
  if (!temporary) {
    source.close();
    return false;
  }
  while (source.available()) {
    uint8_t buffer[256];
    const size_t read = source.read(buffer, sizeof(buffer));
    if (read > 0 && temporary.write(buffer, read) != read) {
      source.close();
      temporary.close();
      SD.remove(QUEUE_TEMP_PATH);
      return false;
    }
  }
  source.close();
  temporary.close();
  if (SD.exists(QUEUE_BACKUP_PATH)) SD.remove(QUEUE_BACKUP_PATH);
  if (!SD.rename(QUEUE_PATH, QUEUE_BACKUP_PATH)) {
    SD.remove(QUEUE_TEMP_PATH);
    return false;
  }
  File remaining = SD.open(QUEUE_TEMP_PATH, FILE_READ);
  const bool hasRemaining = remaining && remaining.size() > 0;
  if (remaining) remaining.close();
  bool replaced = true;
  if (hasRemaining) {
    replaced = SD.rename(QUEUE_TEMP_PATH, QUEUE_PATH);
  } else {
    SD.remove(QUEUE_TEMP_PATH);
  }
  if (!replaced) {
    SD.rename(QUEUE_BACKUP_PATH, QUEUE_PATH);
    return false;
  }
  SD.remove(QUEUE_BACKUP_PATH);
  return true;
}
}  // namespace

bool ThingSpeakPublisher::requestIntervalElapsed() const {
  return !requestStarted_ ||
         millis() - lastRequestStartedAt_ >= MINIMUM_REQUEST_INTERVAL_MS;
}

bool ThingSpeakPublisher::preserveInRam(JsonDocument& record) {
  const char* createdAt = record["created_at"] | "";
  for (size_t offset = 0; offset < pendingCount_; ++offset) {
    const size_t index =
        (pendingHead_ + offset) % RAM_PENDING_CAPACITY;
    const char* pendingCreatedAt =
        pendingRecords_[index]["created_at"] | "";
    if (strcmp(createdAt, pendingCreatedAt) == 0) {
      Serial.printf(
          "ThingSpeak RAM fallback already contains created_at=%s; "
          "duplicate skipped.\n",
          createdAt);
      return true;
    }
  }
  if (pendingCount_ >= RAM_PENDING_CAPACITY) {
    Serial.printf(
        "ThingSpeak RAM fallback is full; created_at=%s could not be "
        "preserved.\n",
        createdAt);
    return false;
  }
  const size_t tail =
      (pendingHead_ + pendingCount_) % RAM_PENDING_CAPACITY;
  pendingRecords_[tail].set(record.as<JsonObjectConst>());
  ++pendingCount_;
  lastPendingRetryAt_ = millis();
  Serial.printf(
      "ThingSpeak record preserved in RAM until SD queue retry: %s.\n",
      createdAt);
  return true;
}

bool ThingSpeakPublisher::queueOrPreserve(JsonDocument& record) {
  const QueueAppendResult result = appendToQueue(record);
  if (result != QueueAppendResult::Failed) {
    return true;
  }
  return preserveInRam(record);
}

void ThingSpeakPublisher::handle() {
  if (pendingCount_ == 0 ||
      millis() - lastPendingRetryAt_ < PENDING_QUEUE_RETRY_INTERVAL_MS) {
    return;
  }
  lastPendingRetryAt_ = millis();
  if (!recoverQueueFiles()) {
    return;
  }
  JsonDocument& record = pendingRecords_[pendingHead_];
  const QueueAppendResult result = appendToQueue(record);
  if (result == QueueAppendResult::Failed) {
    return;
  }
  const char* createdAt = record["created_at"] | "";
  Serial.printf("ThingSpeak RAM fallback moved to SD queue: %s.\n",
                createdAt);
  record.clear();
  pendingHead_ = (pendingHead_ + 1) % RAM_PENDING_CAPACITY;
  --pendingCount_;
}

ThingSpeakPublishResult ThingSpeakPublisher::postSingle(JsonDocument& record) {
  JsonDocument payload;
  payload["api_key"] = THINGSPEAK_WRITE_API_KEY;
  for (JsonPairConst pair : record.as<JsonObjectConst>()) {
    payload[pair.key()] = pair.value();
  }
  String body;
  serializeJson(payload, body);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(REQUEST_TIMEOUT_MS);
  requestStarted_ = true;
  lastRequestStartedAt_ = millis();
  if (!http.begin(client, THINGSPEAK_UPDATE_URL)) {
    Serial.println("Failed to initialize the ThingSpeak request.");
    return ThingSpeakPublishResult::RequestFailed;
  }
  http.addHeader("Content-Type", "application/json");
  const char* createdAt = record["created_at"] | "";
  Serial.printf(
      "Sending weather data to ThingSpeak channel %lu with created_at=%s.\n",
      THINGSPEAK_CHANNEL_ID, createdAt);
  const int statusCode = http.POST(body);
  const String response = http.getString();
  http.end();
  if (statusCode < 200 || statusCode >= 300) {
    Serial.printf("ThingSpeak API returned HTTP %d.\n", statusCode);
    return ThingSpeakPublishResult::RequestFailed;
  }
  JsonDocument responseDocument;
  const DeserializationError error = deserializeJson(responseDocument, response);
  const unsigned long entryId = responseDocument["entry_id"] | 0UL;
  const unsigned long responseChannelId = responseDocument["channel_id"] | 0UL;
  if (error || entryId == 0 || responseChannelId != THINGSPEAK_CHANNEL_ID) {
    Serial.printf("ThingSpeak rejected the update: %s\n", response.c_str());
    return ThingSpeakPublishResult::RequestFailed;
  }
  Serial.printf("Weather data sent to ThingSpeak as entry %lu.\n", entryId);
  return ThingSpeakPublishResult::Sent;
}

ThingSpeakPublishResult ThingSpeakPublisher::sendQueuedBatch() {
  JsonDocument payload;
  payload["write_api_key"] = THINGSPEAK_WRITE_API_KEY;
  JsonArray updates = payload["updates"].to<JsonArray>();
  size_t consumedLines = 0;
  size_t validRecords = 0;
  {
    SdCardGuard sdGuard;
    if (!sdGuard.locked()) {
      Serial.println("SD card is busy; ThingSpeak queue operation was skipped.");
      return ThingSpeakPublishResult::RequestFailed;
    }
    File queue = SD.open(QUEUE_PATH, FILE_READ);
    if (!queue) return ThingSpeakPublishResult::RequestFailed;
    while (queue.available() && consumedLines < MAX_BATCH_RECORDS) {
      String line = queue.readStringUntil('\n');
      ++consumedLines;
      line.trim();
      if (line.isEmpty()) continue;
      JsonDocument record;
      const DeserializationError error = deserializeJson(record, line);
      if (error) {
        Serial.printf("Skipping malformed ThingSpeak queue record: %s\n",
                      error.c_str());
        continue;
      }
      updates.add(record.as<JsonObjectConst>());
      ++validRecords;
    }
    queue.close();
  }
  if (validRecords == 0) {
    discardQueuedLines(consumedLines);
    return ThingSpeakPublishResult::RequestFailed;
  }
  const String url = String("https://api.thingspeak.com/channels/") +
                     String(THINGSPEAK_CHANNEL_ID) + "/bulk_update.json";
  String body;
  serializeJson(payload, body);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(REQUEST_TIMEOUT_MS);
  requestStarted_ = true;
  lastRequestStartedAt_ = millis();
  if (!http.begin(client, url)) {
    Serial.println("Failed to initialize the ThingSpeak bulk request.");
    return ThingSpeakPublishResult::RequestFailed;
  }
  http.addHeader("Content-Type", "application/json");
  Serial.printf("Retrying %u queued ThingSpeak record(s) with Bulk Update.\n",
                static_cast<unsigned int>(validRecords));
  const int statusCode = http.POST(body);
  const String response = http.getString();
  http.end();
  if (statusCode < 200 || statusCode >= 300) {
    Serial.printf("ThingSpeak Bulk API returned HTTP %d.\n", statusCode);
    return ThingSpeakPublishResult::RequestFailed;
  }
  JsonDocument responseDocument;
  const DeserializationError error = deserializeJson(responseDocument, response);
  const bool success = responseDocument["success"] | false;
  if (error || !success) {
    Serial.printf("ThingSpeak rejected the bulk update: %s\n", response.c_str());
    return ThingSpeakPublishResult::RequestFailed;
  }
  if (!discardQueuedLines(consumedLines)) {
    Serial.println("ThingSpeak data was sent but the retry queue update failed.");
    return ThingSpeakPublishResult::RequestFailed;
  }
  Serial.printf("Sent %u ThingSpeak record(s) from the retry queue.\n",
                static_cast<unsigned int>(validRecords));
  return ThingSpeakPublishResult::Sent;
}

ThingSpeakPublishResult ThingSpeakPublisher::publish(
    time_t observedAt, float temperature, int humidity, int pressure,
    int weatherConditionId, uint8_t precipitationProbability,
    int temperatureAlertThreshold, int wifiRssi, bool rainAlertActive) {
  if (!credentialsAreSet()) {
    Serial.println("ThingSpeak upload skipped because credentials are not set.");
    return ThingSpeakPublishResult::CredentialsMissing;
  }
  if (observedAt < MINIMUM_VALID_TIME) {
    Serial.println("ThingSpeak upload skipped because time is not synchronized.");
    return ThingSpeakPublishResult::TimeUnavailable;
  }
  JsonDocument record;
  buildRecord(record, observedAt, temperature, humidity, pressure,
              weatherConditionId, precipitationProbability,
              temperatureAlertThreshold, wifiRssi, rainAlertActive);
  if (!recoverQueueFiles()) {
    preserveInRam(record);
    return ThingSpeakPublishResult::RequestFailed;
  }
  const bool queuedRecordsExist = queueHasRecords();
  if (queuedRecordsExist && !queueOrPreserve(record)) {
    return ThingSpeakPublishResult::RequestFailed;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak upload deferred because Wi-Fi is disconnected.");
    if (!queuedRecordsExist) queueOrPreserve(record);
    return ThingSpeakPublishResult::WiFiDisconnected;
  }
  if (!requestIntervalElapsed()) {
    const unsigned long waitMs =
        MINIMUM_REQUEST_INTERVAL_MS - (millis() - lastRequestStartedAt_);
    Serial.printf(
        "ThingSpeak upload deferred for %lu ms to preserve the request "
        "interval.\n",
        waitMs);
    if (!queuedRecordsExist) queueOrPreserve(record);
    return ThingSpeakPublishResult::RequestFailed;
  }
  if (queuedRecordsExist) return sendQueuedBatch();
  const ThingSpeakPublishResult result = postSingle(record);
  if (result != ThingSpeakPublishResult::Sent) queueOrPreserve(record);
  return result;
}
