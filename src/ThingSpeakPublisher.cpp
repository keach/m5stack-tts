#include "ThingSpeakPublisher.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "thingspeak_secrets.h"

namespace {
constexpr char THINGSPEAK_UPDATE_URL[] =
    "https://api.thingspeak.com/update.json";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
constexpr uint16_t REQUEST_TIMEOUT_MS = 10000;

bool credentialsAreSet() {
  return THINGSPEAK_CHANNEL_ID != 0 && THINGSPEAK_WRITE_API_KEY[0] != '\0';
}

void formatCreatedAt(time_t observedAt, char* output, size_t outputSize) {
  tm utcTime = {};
  gmtime_r(&observedAt, &utcTime);
  strftime(output, outputSize, "%Y-%m-%dT%H:%M:%SZ", &utcTime);
}
}  // namespace

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
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ThingSpeak upload skipped because Wi-Fi is disconnected.");
    return ThingSpeakPublishResult::WiFiDisconnected;
  }

  char createdAt[21];
  formatCreatedAt(observedAt, createdAt, sizeof(createdAt));
  JsonDocument payload;
  payload["api_key"] = THINGSPEAK_WRITE_API_KEY;
  payload["created_at"] = createdAt;
  payload["field1"] = temperature;
  payload["field2"] = humidity;
  payload["field3"] = pressure;
  payload["field4"] = weatherConditionId;
  payload["field5"] = precipitationProbability;
  payload["field6"] = temperatureAlertThreshold;
  payload["field7"] = wifiRssi;
  payload["field8"] = rainAlertActive ? 1 : 0;

  String body;
  serializeJson(payload, body);
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(REQUEST_TIMEOUT_MS);
  if (!http.begin(client, THINGSPEAK_UPDATE_URL)) {
    Serial.println("Failed to initialize the ThingSpeak request.");
    return ThingSpeakPublishResult::RequestFailed;
  }
  http.addHeader("Content-Type", "application/json");

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
  const DeserializationError error =
      deserializeJson(responseDocument, response);
  const unsigned long entryId = responseDocument["entry_id"] | 0UL;
  const unsigned long responseChannelId =
      responseDocument["channel_id"] | 0UL;
  if (error || entryId == 0 || responseChannelId != THINGSPEAK_CHANNEL_ID) {
    Serial.printf("ThingSpeak rejected the update: %s\n", response.c_str());
    return ThingSpeakPublishResult::RequestFailed;
  }

  Serial.printf("Weather data sent to ThingSpeak as entry %lu.\n", entryId);
  return ThingSpeakPublishResult::Sent;
}
