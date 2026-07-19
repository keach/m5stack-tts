#include "AmbientPublisher.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ambient_secrets.h"

namespace {
constexpr char AMBIENT_API_HOST[] = "https://ambidata.io";
constexpr char QUEUE_PATH[] = "/ambient_queue.ndjson";
constexpr char QUEUE_TEMP_PATH[] = "/ambient_queue.tmp";
constexpr char QUEUE_BACKUP_PATH[] = "/ambient_queue.bak";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
constexpr uint16_t REQUEST_TIMEOUT_MS = 10000;
constexpr size_t MAX_BATCH_RECORDS = 10;

bool credentialsAreSet() {
  return AMBIENT_CHANNEL_ID != 0 && AMBIENT_WRITE_KEY[0] != '\0';
}

void formatCreated(time_t observedAt, char* output, size_t outputSize) {
  tm timeInfo = {};
  localtime_r(&observedAt, &timeInfo);
  strftime(output, outputSize, "%Y-%m-%d %H:%M:%S", &timeInfo);
}

void buildRecord(JsonDocument& record, time_t observedAt,
                 const char* condition, float temperature, int humidity,
                 int pressure, float rainLastHour,
                 int temperatureAlertThreshold, bool rainingNow,
                 int wifiRssi) {
  char created[20];
  formatCreated(observedAt, created, sizeof(created));
  record["created"] = created;
  record["d1"] = temperature;
  record["d2"] = humidity;
  record["d3"] = pressure;
  record["d4"] = rainLastHour;
  record["d5"] = temperatureAlertThreshold;
  record["d6"] = rainingNow ? 1 : 0;
  record["d7"] = wifiRssi;
  record["cmnt"] = condition;
}

AmbientPublishResult postPayload(JsonDocument& payload, const char* endpoint) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Ambient upload deferred because Wi-Fi is disconnected.");
    return AmbientPublishResult::WiFiDisconnected;
  }

  String body;
  serializeJson(payload, body);
  String url = String(AMBIENT_API_HOST) + "/api/v2/channels/" +
               String(AMBIENT_CHANNEL_ID) + endpoint;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(REQUEST_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    Serial.println("Failed to initialize the Ambient request.");
    return AmbientPublishResult::RequestFailed;
  }
  http.addHeader("Content-Type", "application/json");

  const int statusCode = http.POST(body);
  http.end();
  if (statusCode < 200 || statusCode >= 300) {
    Serial.printf("Ambient API returned HTTP %d.\n", statusCode);
    return AmbientPublishResult::RequestFailed;
  }
  return AmbientPublishResult::Sent;
}

bool recoverQueueFiles() {
  if (!SD.exists(QUEUE_PATH) && SD.exists(QUEUE_BACKUP_PATH)) {
    if (!SD.rename(QUEUE_BACKUP_PATH, QUEUE_PATH)) {
      Serial.println("Failed to recover the Ambient retry queue.");
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
  if (!SD.exists(QUEUE_PATH)) {
    return false;
  }
  File file = SD.open(QUEUE_PATH, FILE_READ);
  if (!file) {
    return false;
  }
  const bool hasRecords = file.size() > 0;
  file.close();
  return hasRecords;
}

bool appendToQueue(JsonDocument& record) {
  File file = SD.open(QUEUE_PATH, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open the Ambient retry queue.");
    return false;
  }
  const size_t written = serializeJson(record, file);
  file.println();
  file.close();
  if (written == 0) {
    Serial.println("Failed to append data to the Ambient retry queue.");
    return false;
  }
  Serial.println("Weather data saved to the Ambient retry queue.");
  return true;
}

bool discardQueuedLines(size_t lineCount) {
  File source = SD.open(QUEUE_PATH, FILE_READ);
  if (!source) {
    return false;
  }

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

  if (SD.exists(QUEUE_BACKUP_PATH)) {
    SD.remove(QUEUE_BACKUP_PATH);
  }
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

AmbientPublishResult sendQueuedBatch() {
  File queue = SD.open(QUEUE_PATH, FILE_READ);
  if (!queue) {
    return AmbientPublishResult::RequestFailed;
  }

  JsonDocument payload;
  payload["writeKey"] = AMBIENT_WRITE_KEY;
  JsonArray data = payload["data"].to<JsonArray>();
  size_t consumedLines = 0;
  size_t validRecords = 0;
  while (queue.available() && consumedLines < MAX_BATCH_RECORDS) {
    String line = queue.readStringUntil('\n');
    ++consumedLines;
    line.trim();
    if (line.isEmpty()) {
      continue;
    }
    JsonDocument record;
    const DeserializationError error = deserializeJson(record, line);
    if (error) {
      Serial.printf("Skipping malformed Ambient queue record: %s\n",
                    error.c_str());
      continue;
    }
    data.add(record.as<JsonObjectConst>());
    ++validRecords;
  }
  queue.close();

  if (validRecords == 0) {
    discardQueuedLines(consumedLines);
    return AmbientPublishResult::RequestFailed;
  }

  Serial.printf("Retrying %u queued Ambient record(s).\n",
                static_cast<unsigned int>(validRecords));
  const AmbientPublishResult result = postPayload(payload, "/dataarray");
  if (result != AmbientPublishResult::Sent) {
    return result;
  }
  if (!discardQueuedLines(consumedLines)) {
    Serial.println("Ambient data was sent but the retry queue update failed.");
    return AmbientPublishResult::RequestFailed;
  }

  Serial.printf("Sent %u Ambient record(s) from the retry queue.\n",
                static_cast<unsigned int>(validRecords));
  return AmbientPublishResult::Sent;
}
}  // namespace

AmbientPublishResult AmbientPublisher::publish(
    time_t observedAt, const char* condition, float temperature, int humidity,
    int pressure, float rainLastHour, int temperatureAlertThreshold,
    bool rainingNow, int wifiRssi) {
  if (!credentialsAreSet()) {
    Serial.println("Ambient upload skipped because credentials are not set.");
    return AmbientPublishResult::CredentialsMissing;
  }
  if (observedAt < MINIMUM_VALID_TIME) {
    Serial.println("Ambient upload skipped because time is not synchronized.");
    return AmbientPublishResult::TimeUnavailable;
  }

  JsonDocument record;
  buildRecord(record, observedAt, condition, temperature, humidity, pressure,
              rainLastHour, temperatureAlertThreshold, rainingNow, wifiRssi);

  if (!recoverQueueFiles()) {
    return AmbientPublishResult::RequestFailed;
  }

  if (queueHasRecords()) {
    if (!appendToQueue(record)) {
      return AmbientPublishResult::RequestFailed;
    }
    return sendQueuedBatch();
  }

  JsonDocument payload;
  payload["writeKey"] = AMBIENT_WRITE_KEY;
  for (JsonPairConst pair : record.as<JsonObjectConst>()) {
    payload[pair.key()] = pair.value();
  }

  char created[20];
  formatCreated(observedAt, created, sizeof(created));
  Serial.printf("Sending weather data to Ambient with created=%s.\n", created);
  const AmbientPublishResult result = postPayload(payload, "/data");
  if (result == AmbientPublishResult::Sent) {
    Serial.println("Weather data sent to Ambient.");
    return result;
  }

  appendToQueue(record);
  return result;
}
