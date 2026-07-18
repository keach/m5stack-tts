#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Stack.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <aquestalk.h>
#include <time.h>

#include "secrets.h"
#include "AmbientPublisher.h"
#include "AppSettings.h"
#include "RainAlertService.h"
#include "SettingsMode.h"
#include "SpeechService.h"
#include "SpeechNumberFormatter.h"
#include "TemperatureAlertService.h"

namespace {
constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
constexpr long JST_OFFSET_SECONDS = 9 * 60 * 60;
constexpr int DAYLIGHT_OFFSET_SECONDS = 0;
constexpr unsigned long NTP_TIMEOUT_MS = 15000;
constexpr char NTP_SERVER_PRIMARY[] = "ntp.nict.jp";
constexpr char NTP_SERVER_SECONDARY[] = "pool.ntp.org";
constexpr unsigned long WEATHER_UPDATE_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr unsigned long MANUAL_WEATHER_MIN_INTERVAL_MS = 30UL * 1000UL;
constexpr unsigned long BUTTON_CONFIRMATION_MS = 80;
constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 1000;
constexpr unsigned long SPLASH_DURATION_MS = 3000;
constexpr unsigned long SETTINGS_ENTRY_HOLD_MS = 1000;
constexpr char WEATHER_API_URL[] =
    "https://api.openweathermap.org/data/2.5/weather";
constexpr int SD_CS_PIN = 4;
constexpr int SD_SCK_PIN = 18;
constexpr int SD_MISO_PIN = 19;
constexpr int SD_MOSI_PIN = 23;
constexpr uint32_t SD_FREQUENCY_HZ = 25000000;
constexpr char WEATHER_LOG_PATH[] = "/weather.csv";

struct WeatherData {
  char condition[32] = "--";
  float temperature = 0;
  int humidity = 0;
  int pressure = 0;
  float rainLastHour = 0;
  bool valid = false;
};

enum class WeatherRequestSource {
  Startup,
  ManualButton,
  Scheduled,
};

WeatherData weather;
unsigned long lastWeatherAttempt = 0;
bool weatherAttempted = false;
unsigned long lastDisplayUpdate = 0;
unsigned long buttonAPressDetectedAt = 0;
bool buttonAConfirmationPending = false;
ClockDisplayPrecision clockDisplayPrecision = ClockDisplayPrecision::Minutes;
bool storageAvailable = false;
AppSettings appSettings;
SettingsMode settingsMode;
SpeechService speech;
bool speechAvailable = false;
TemperatureAlertService temperatureAlerts;
RainAlertService rainAlerts;
AmbientPublisher ambientPublisher;

bool showSplashScreen() {
  M5.Lcd.fillScreen(TFT_NAVY);
  M5.Lcd.drawRect(8, 8, 304, 224, TFT_CYAN);
  M5.Lcd.drawRect(12, 12, 296, 216, TFT_DARKCYAN);

  M5.Lcd.setTextColor(TFT_CYAN, TFT_NAVY);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(28, 48);
  M5.Lcd.println("M5 WEATHER TTS");

  M5.Lcd.drawFastHLine(28, 88, 264, TFT_DARKCYAN);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(32, 108);
  M5.Lcd.println("Weather monitor");
  M5.Lcd.setCursor(32, 134);
  M5.Lcd.println("and voice alerts");

  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(88, 180);
  M5.Lcd.println("Hold B for settings");
  M5.Lcd.setCursor(112, 196);
  M5.Lcd.println("Initializing...");
  M5.Lcd.setCursor(116, 216);
  M5.Lcd.println("M5Stack Basic");

  const unsigned long startedAt = millis();
  unsigned long buttonBHeldAt = 0;
  bool settingsRequested = false;
  while (millis() - startedAt < SPLASH_DURATION_MS) {
    M5.update();
    if (M5.BtnB.isPressed()) {
      if (buttonBHeldAt == 0) {
        buttonBHeldAt = millis();
      } else if (!settingsRequested &&
                 millis() - buttonBHeldAt >= SETTINGS_ENTRY_HOLD_MS) {
        settingsRequested = true;
        M5.Lcd.fillRect(70, 174, 180, 36, TFT_NAVY);
        M5.Lcd.setTextColor(TFT_GREEN, TFT_NAVY);
        M5.Lcd.setCursor(91, 186);
        M5.Lcd.print("Settings requested");
      }
    } else if (!settingsRequested) {
      buttonBHeldAt = 0;
    }
    delay(10);
  }

  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(20, 40);
  M5.Lcd.println("Starting services...");
  return settingsRequested;
}

bool initializeStorage() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, SPI, SD_FREQUENCY_HZ)) {
    Serial.println("SD card initialization failed. Logging is disabled.");
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    Serial.println("SD card not detected. Logging is disabled.");
    return false;
  }

  const uint64_t capacityMb = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("SD card ready. Capacity: %llu MB\n", capacityMb);
  return true;
}

bool appendWeatherLog(const WeatherData& data, time_t observedAt) {
  if (!storageAvailable) {
    return false;
  }

  const bool needsHeader = !SD.exists(WEATHER_LOG_PATH);
  File file = SD.open(WEATHER_LOG_PATH, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open weather.csv. Logging is disabled.");
    storageAvailable = false;
    return false;
  }

  if (needsHeader) {
    file.println(
        "datetime,weather,temp_c,humidity_pct,pressure_hpa,rain_1h_mm");
  }

  char formattedTime[20] = "unknown";
  if (observedAt >= 1600000000) {
    tm timeInfo = {};
    localtime_r(&observedAt, &timeInfo);
    strftime(formattedTime, sizeof(formattedTime), "%Y-%m-%d %H:%M:%S",
             &timeInfo);
  }

  const size_t written =
      file.printf("%s,%s,%.1f,%d,%d,%.1f\n", formattedTime, data.condition,
                  data.temperature, data.humidity, data.pressure,
                  data.rainLastHour);
  file.close();

  if (written == 0) {
    Serial.println("Failed to write weather data to the SD card.");
    return false;
  }

  Serial.printf("Weather data appended to %s.\n", WEATHER_LOG_PATH);
  return true;
}

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
  char formattedTime[32] = "Time unavailable";
  if (getLocalTime(&timeInfo, 10)) {
    constexpr const char* WEEKDAYS[] = {"Sun", "Mon", "Tue", "Wed",
                                        "Thu", "Fri", "Sat"};
    const char* weekday =
        timeInfo.tm_wday >= 0 && timeInfo.tm_wday < 7
            ? WEEKDAYS[timeInfo.tm_wday]
            : "---";
    if (clockDisplayPrecision == ClockDisplayPrecision::Seconds) {
      snprintf(formattedTime, sizeof(formattedTime),
               "%04d.%02d.%02d. %s %02d:%02d:%02d", timeInfo.tm_year + 1900,
               timeInfo.tm_mon + 1, timeInfo.tm_mday, weekday,
               timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
    } else {
      snprintf(formattedTime, sizeof(formattedTime),
               "%04d.%02d.%02d. %s %02d:%02d", timeInfo.tm_year + 1900,
               timeInfo.tm_mon + 1, timeInfo.tm_mday, weekday,
               timeInfo.tm_hour, timeInfo.tm_min);
    }
  }

  M5.Lcd.fillRect(0, 0, 320, 32, TFT_NAVY);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setTextSize(2);
  const int textWidth = M5.Lcd.textWidth(formattedTime);
  M5.Lcd.setCursor(max(0, (320 - textWidth) / 2), 8);
  M5.Lcd.print(formattedTime);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
}

void drawWeather() {
  M5.Lcd.fillRect(0, 32, 320, 208, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  const int temperatureAlert =
      weather.valid ? temperatureAlerts.activeThreshold(weather.temperature) : 0;
  M5.Lcd.setTextColor(temperatureAlert >= 35 ? TFT_RED
                                             : temperatureAlert >= 30 ? TFT_ORANGE
                                                                      : TFT_CYAN,
                      TFT_BLACK);
  M5.Lcd.setCursor(16, 44);
  if (temperatureAlert > 0) {
    M5.Lcd.printf("HIGH TEMP ALERT: %d C", temperatureAlert);
  } else if (weather.valid && rainAlerts.isRainActive()) {
    M5.Lcd.printf("RAIN ALERT: %.1f mm", weather.rainLastHour);
  } else {
    M5.Lcd.println("CURRENT WEATHER");
  }
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  if (!weather.valid) {
    M5.Lcd.setCursor(16, 86);
    M5.Lcd.println("Weather unavailable");
    M5.Lcd.setCursor(16, 214);
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("A: refresh  B: speak  C: stop");
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
  M5.Lcd.print("A: refresh  B: speak  C: stop");
}

const char* weatherConditionInJapanese(const char* condition) {
  if (strcmp(condition, "Clear") == 0) return "晴れ";
  if (strcmp(condition, "Clouds") == 0) return "曇り";
  if (strcmp(condition, "Rain") == 0) return "雨";
  if (strcmp(condition, "Drizzle") == 0) return "小雨";
  if (strcmp(condition, "Thunderstorm") == 0) return "雷雨";
  if (strcmp(condition, "Snow") == 0) return "雪";
  if (strcmp(condition, "Mist") == 0 || strcmp(condition, "Fog") == 0 ||
      strcmp(condition, "Haze") == 0) {
    return "霧";
  }
  return "不明";
}

bool isRainingCondition(const char* condition) {
  return strcmp(condition, "Rain") == 0 || strcmp(condition, "Drizzle") == 0 ||
         strcmp(condition, "Thunderstorm") == 0;
}

void speakCurrentWeather() {
  if (!speechAvailable) {
    Serial.println("Speech service is unavailable.");
    return;
  }
  if (!weather.valid) {
    speech.speak("音声合成のテストです。");
    return;
  }

  tm timeInfo = {};
  char dateTimeText[96] = "現在時刻は取得できません。";
  if (getLocalTime(&timeInfo, 10)) {
    snprintf(dateTimeText, sizeof(dateTimeText),
             "現在は%d年%d月%d日、%d時%d分です。", timeInfo.tm_year + 1900,
             timeInfo.tm_mon + 1, timeInfo.tm_mday, timeInfo.tm_hour,
             timeInfo.tm_min);
  }

  char pressureText[32];
  char rainText[24];
  SpeechNumberFormatter::formatInteger(weather.pressure, pressureText,
                                       sizeof(pressureText));
  SpeechNumberFormatter::formatOneDecimal(weather.rainLastHour, rainText,
                                          sizeof(rainText));

  char message[384];
  snprintf(message, sizeof(message),
           "%s現在の天気は%sです。気温は%.1f度、湿度は%dパーセント、"
           "気圧は%sヘクトパスカル、1時間雨量は%sミリです。",
           dateTimeText, weatherConditionInJapanese(weather.condition),
           weather.temperature, weather.humidity, pressureText, rainText);
  Serial.printf("Speaking: %s\n", message);
  if (!speech.speak(message)) {
    Serial.println("Failed to start speech.");
  }
}

const char* weatherRequestSourceName(WeatherRequestSource source) {
  switch (source) {
    case WeatherRequestSource::Startup:
      return "startup";
    case WeatherRequestSource::ManualButton:
      return "button A";
    case WeatherRequestSource::Scheduled:
      return "timer";
  }
  return "unknown";
}

bool fetchWeather(WeatherRequestSource source) {
  const unsigned long now = millis();
  if (source == WeatherRequestSource::ManualButton && weatherAttempted &&
      now - lastWeatherAttempt < MANUAL_WEATHER_MIN_INTERVAL_MS) {
    const unsigned long remainingSeconds =
        (MANUAL_WEATHER_MIN_INTERVAL_MS - (now - lastWeatherAttempt) + 999) /
        1000;
    Serial.printf(
        "Weather request from button A ignored; retry in %lu seconds.\n",
        remainingSeconds);
    return false;
  }

  lastWeatherAttempt = now;
  weatherAttempted = true;
  Serial.printf("Weather request source: %s.\n",
                weatherRequestSourceName(source));
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

  const time_t observedAt = time(nullptr);
  tm localTime = {};
  const bool timeAvailable = getLocalTime(&localTime, 10);
  const bool quietHours = !timeAvailable || localTime.tm_hour < 6;
  const bool temperatureAudioPlayed = temperatureAlerts.evaluate(
      weather.temperature, speechAvailable && !quietHours, speech);
  rainAlerts.evaluate(isRainingCondition(weather.condition), weather.condition,
                      weather.rainLastHour,
                      speechAvailable && !quietHours && !temperatureAudioPlayed,
                      speech);

  Serial.printf("Weather updated: %s, %.1f C, %d %%, %d hPa, %.1f mm/h\n",
                weather.condition, weather.temperature, weather.humidity,
                weather.pressure, weather.rainLastHour);
  appendWeatherLog(weather, observedAt);
  ambientPublisher.publish(
      observedAt, weather.condition, weather.temperature, weather.humidity,
      weather.pressure, weather.rainLastHour,
      temperatureAlerts.activeThreshold(weather.temperature),
      isRainingCondition(weather.condition), WiFi.RSSI());
  drawWeather();
  return true;
}
}  // namespace

void setup() {
  // Initialize the display and serial port here; SD is initialized separately
  // so that card detection and errors can be handled explicitly.
  M5.begin(true, false, true);
  Serial.begin(115200);

  appSettings.begin();
  clockDisplayPrecision = appSettings.clockPrecision();
  speech.setVolumePercent(appSettings.volumePercent());
  const bool settingsRequested = showSplashScreen();

  storageAvailable = initializeStorage();
  speechAvailable = storageAvailable && speech.begin();
  temperatureAlerts.begin();
  rainAlerts.begin();
  connectToWiFi();
  syncTimeWithNtp();
  drawDateTime();
  drawWeather();
  fetchWeather(WeatherRequestSource::Startup);

  if (settingsRequested) {
    tm diagnosticTime = {};
    const DiagnosticStatus diagnostics = {
        storageAvailable,
        storageAvailable && SD.exists("/aq_dic/aqdic_m.bin"),
        speechAvailable,
        WiFi.status() == WL_CONNECTED,
        getLocalTime(&diagnosticTime, 10),
        weather.valid,
    };
    settingsMode.run(appSettings, speech, speechAvailable, diagnostics);
    clockDisplayPrecision = appSettings.clockPrecision();
    speech.setVolumePercent(appSettings.volumePercent());
    drawDateTime();
    drawWeather();
  }
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    buttonAPressDetectedAt = millis();
    buttonAConfirmationPending = true;
    Serial.printf("Button A signal detected (raw pin: %d).\n",
                  digitalRead(BUTTON_A_PIN));
  }
  if (buttonAConfirmationPending) {
    if (M5.BtnA.isReleased()) {
      buttonAConfirmationPending = false;
      Serial.println("Button A signal rejected as too short.");
    } else if (millis() - buttonAPressDetectedAt >= BUTTON_CONFIRMATION_MS) {
      buttonAConfirmationPending = false;
      Serial.println("Button A press confirmed.");
      fetchWeather(WeatherRequestSource::ManualButton);
    }
  }
  if (M5.BtnB.wasPressed()) {
    speakCurrentWeather();
  }
  if (M5.BtnC.wasPressed()) {
    speech.stop();
  }

  const unsigned long now = millis();
  if (now - lastWeatherAttempt >= WEATHER_UPDATE_INTERVAL_MS) {
    fetchWeather(WeatherRequestSource::Scheduled);
  }
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL_MS) {
    lastDisplayUpdate = now;
    drawDateTime();
  }
}
