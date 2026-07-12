#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "secrets.h"

namespace {
constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
constexpr long JST_OFFSET_SECONDS = 9 * 60 * 60;
constexpr int DAYLIGHT_OFFSET_SECONDS = 0;
constexpr unsigned long NTP_TIMEOUT_MS = 15000;
constexpr char NTP_SERVER_PRIMARY[] = "ntp.nict.jp";
constexpr char NTP_SERVER_SECONDARY[] = "pool.ntp.org";
constexpr unsigned long WEATHER_UPDATE_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 1000;
constexpr char WEATHER_API_URL[] =
    "https://api.openweathermap.org/data/2.5/weather";

struct WeatherData {
  char condition[32] = "--";
  float temperature = 0;
  int humidity = 0;
  int pressure = 0;
  float rainLastHour = 0;
  bool valid = false;
};

WeatherData weather;
unsigned long lastWeatherAttempt = 0;
unsigned long lastDisplayUpdate = 0;

void connectToWiFi() {
  M5.Lcd.setCursor(20, 100);
  M5.Lcd.print("Wi-Fi: connecting");
  Serial.printf("Connecting to Wi-Fi SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < WIFI_TIMEOUT_MS) {
    delay(500);
    M5.Lcd.print(".");
    Serial.print(".");
  }
  Serial.println();

  M5.Lcd.setCursor(20, 130);
  if (WiFi.status() == WL_CONNECTED) {
    const IPAddress ip = WiFi.localIP();
    M5.Lcd.printf("IP: %s", ip.toString().c_str());
    Serial.printf("Wi-Fi connected. IP: %s\n", ip.toString().c_str());
  } else {
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.print("Wi-Fi: failed");
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    Serial.println("Wi-Fi connection timed out.");
  }
}

void syncTimeWithNtp() {
  M5.Lcd.setCursor(20, 160);
  if (WiFi.status() != WL_CONNECTED) {
    M5.Lcd.print("NTP: skipped");
    Serial.println("NTP sync skipped because Wi-Fi is disconnected.");
    return;
  }

  M5.Lcd.print("NTP: syncing...");
  Serial.println("Synchronizing time with NTP...");
  configTime(JST_OFFSET_SECONDS, DAYLIGHT_OFFSET_SECONDS,
             NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);

  tm timeInfo = {};
  const unsigned long startedAt = millis();
  bool synchronized = false;
  while (millis() - startedAt < NTP_TIMEOUT_MS) {
    if (getLocalTime(&timeInfo, 1000)) {
      synchronized = true;
      break;
    }
    Serial.print(".");
  }
  Serial.println();

  M5.Lcd.setCursor(20, 190);
  if (synchronized) {
    char formattedTime[20];
    strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S",
             &timeInfo);
    M5.Lcd.printf("JST: %s", formattedTime);
    Serial.printf("NTP synchronized: %s JST\n", formattedTime);
  } else {
    M5.Lcd.setTextColor(TFT_RED, TFT_BLACK);
    M5.Lcd.print("NTP: failed");
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    Serial.println("NTP synchronization timed out.");
  }
}

void drawDateTime() {
  tm timeInfo = {};
  char formattedTime[24] = "Time unavailable";
  if (getLocalTime(&timeInfo, 10)) {
    strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S",
             &timeInfo);
  }

  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(38, 8);
  M5.Lcd.print(formattedTime);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
}

void drawWeather() {
  M5.Lcd.fillRect(0, 32, 320, 208, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Lcd.setCursor(16, 44);
  M5.Lcd.println("CURRENT WEATHER");
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  if (!weather.valid) {
    M5.Lcd.setCursor(16, 86);
    M5.Lcd.println("Weather unavailable");
    M5.Lcd.setCursor(16, 214);
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("Btn A: refresh now");
    return;
  }

  M5.Lcd.setCursor(16, 76);
  M5.Lcd.printf("Weather : %s", weather.condition);
  M5.Lcd.setCursor(16, 104);
  M5.Lcd.printf("Temp    : %.1f C", weather.temperature);
  M5.Lcd.setCursor(16, 132);
  M5.Lcd.printf("Humidity: %d %%", weather.humidity);
  M5.Lcd.setCursor(16, 160);
  M5.Lcd.printf("Pressure: %d hPa", weather.pressure);
  M5.Lcd.setCursor(16, 188);
  M5.Lcd.printf("Rain 1h : %.1f mm", weather.rainLastHour);
  M5.Lcd.setCursor(16, 222);
  M5.Lcd.setTextSize(1);
  M5.Lcd.print("Updates every 10 min / Btn A: refresh");
}

bool fetchWeather() {
  lastWeatherAttempt = millis();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Weather update skipped because Wi-Fi is disconnected.");
    return false;
  }

  String url = String(WEATHER_API_URL) + "?lat=" +
               String(WEATHER_LATITUDE, 6) + "&lon=" +
               String(WEATHER_LONGITUDE, 6) + "&appid=" +
               OPENWEATHER_API_KEY + "&units=metric&lang=en";

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("Failed to initialize the weather request.");
    return false;
  }

  Serial.println("Requesting current weather...");
  const int statusCode = http.GET();
  if (statusCode != HTTP_CODE_OK) {
    Serial.printf("Weather API returned HTTP %d.\n", statusCode);
    http.end();
    return false;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, http.getStream());
  if (error) {
    Serial.printf("Weather JSON parsing failed: %s\n", error.c_str());
    http.end();
    return false;
  }

  strlcpy(weather.condition, document["weather"][0]["main"] | "Unknown",
          sizeof(weather.condition));
  weather.temperature = document["main"]["temp"] | 0.0F;
  weather.humidity = document["main"]["humidity"] | 0;
  weather.pressure = document["main"]["pressure"] | 0;
  weather.rainLastHour = document["rain"]["1h"] | 0.0F;
  weather.valid = true;
  http.end();

  Serial.printf("Weather updated: %s, %.1f C, %d %%, %d hPa, %.1f mm/h\n",
                weather.condition, weather.temperature, weather.humidity,
                weather.pressure, weather.rainLastHour);
  drawWeather();
  return true;
}
}  // namespace

void setup() {
  M5.begin();
  Serial.begin(115200);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(20, 40);
  M5.Lcd.println("M5Stack Basic");
  M5.Lcd.setCursor(20, 70);
  M5.Lcd.println("PlatformIO ready!");

  connectToWiFi();
  syncTimeWithNtp();
  drawDateTime();
  drawWeather();
  fetchWeather();
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    fetchWeather();
  }

  const unsigned long now = millis();
  if (now - lastWeatherAttempt >= WEATHER_UPDATE_INTERVAL_MS) {
    fetchWeather();
  }
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL_MS) {
    lastDisplayUpdate = now;
    drawDateTime();
  }
}
