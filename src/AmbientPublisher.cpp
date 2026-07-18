#include "AmbientPublisher.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "ambient_secrets.h"

namespace {
constexpr char AMBIENT_API_HOST[] = "https://ambidata.io";
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
constexpr uint16_t REQUEST_TIMEOUT_MS = 10000;
}  // namespace

bool AmbientPublisher::publish(time_t observedAt, const char* condition,
                               float temperature, int humidity, int pressure,
                               float rainLastHour,
                               int temperatureAlertThreshold, bool rainingNow,
                               int wifiRssi) {
  if (AMBIENT_CHANNEL_ID == 0 || AMBIENT_WRITE_KEY[0] == '\0') {
    Serial.println("Ambient upload skipped because credentials are not set.");
    return false;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Ambient upload skipped because Wi-Fi is disconnected.");
    return false;
  }
  if (observedAt < MINIMUM_VALID_TIME) {
    Serial.println("Ambient upload skipped because time is not synchronized.");
    return false;
  }

  tm timeInfo = {};
  localtime_r(&observedAt, &timeInfo);
  char created[20];
  strftime(created, sizeof(created), "%Y-%m-%d %H:%M:%S", &timeInfo);

  JsonDocument payload;
  payload["writeKey"] = AMBIENT_WRITE_KEY;
  payload["created"] = created;
  payload["d1"] = temperature;
  payload["d2"] = humidity;
  payload["d3"] = pressure;
  payload["d4"] = rainLastHour;
  payload["d5"] = temperatureAlertThreshold;
  payload["d6"] = rainingNow ? 1 : 0;
  payload["d7"] = wifiRssi;
  payload["cmnt"] = condition;

  String body;
  serializeJson(payload, body);
  String url = String(AMBIENT_API_HOST) + "/api/v2/channels/" +
               String(AMBIENT_CHANNEL_ID) + "/data";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(REQUEST_TIMEOUT_MS);
  if (!http.begin(client, url)) {
    Serial.println("Failed to initialize the Ambient request.");
    return false;
  }
  http.addHeader("Content-Type", "application/json");

  Serial.printf("Sending weather data to Ambient with created=%s.\n", created);
  const int statusCode = http.POST(body);
  http.end();
  if (statusCode < 200 || statusCode >= 300) {
    Serial.printf("Ambient API returned HTTP %d.\n", statusCode);
    return false;
  }

  Serial.println("Weather data sent to Ambient.");
  return true;
}
