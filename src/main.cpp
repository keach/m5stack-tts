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
#include "earthquake_config.h"
#include "AmbientPublisher.h"
#include "AppSettings.h"
#include "EarthquakeService.h"
#include "EarthquakeHistoryService.h"
#include "EarthquakeHistoryReader.h"
#include "RainAlertService.h"
#include "RainForecastAlertService.h"
#include "RuntimeDiagnostics.h"
#include "SdCardLock.h"
#include "SettingsMode.h"
#include "FirmwareInfo.h"
#include "JapaneseFont.h"
#include "SpeechService.h"
#include "SpeechNumberFormatter.h"
#include "TemperatureAlertService.h"
#include "ThingSpeakPublisher.h"
#include "WebDownloadServer.h"

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
constexpr unsigned long FORECAST_SCREEN_TIMEOUT_MS = 60UL * 1000UL;
constexpr unsigned long SPLASH_DURATION_MS = 3000;
constexpr unsigned long SETTINGS_ENTRY_HOLD_MS = 1000;
constexpr time_t MINIMUM_VALID_TIME = 1600000000;
constexpr char WEATHER_API_URL[] =
    "https://api.openweathermap.org/data/2.5/weather";
constexpr char FORECAST_API_URL[] =
    "https://api.openweathermap.org/data/2.5/forecast";
constexpr size_t FORECAST_ENTRY_COUNT = 4;
constexpr int SD_CS_PIN = 4;
constexpr int SD_SCK_PIN = 18;
constexpr int SD_MISO_PIN = 19;
constexpr int SD_MOSI_PIN = 23;
constexpr uint32_t SD_FREQUENCY_HZ = 25000000;
constexpr char WEATHER_LOG_PATH[] = "/weather.csv";
constexpr unsigned long LOG_RETRY_INTERVAL_MS = 60UL * 1000UL;
constexpr uint8_t LOG_RETRY_LIMIT = 3;
constexpr unsigned long JAPANESE_FONT_RELOAD_RETRY_MS = 1000;

struct WeatherData {
  char condition[32] = "--";
  int conditionId = 0;
  int cloudiness = -1;
  float temperature = 0;
  int humidity = 0;
  int pressure = 0;
  float rainLastHour = 0;
  time_t observedAt = 0;
  bool valid = false;
};

struct ForecastEntry {
  time_t forecastAt = 0;
  char condition[24] = "--";
  int conditionId = 0;
  int cloudiness = -1;
  float temperature = 0;
  uint8_t precipitationProbability = 0;
  float rainThreeHours = 0;
};

struct ForecastData {
  ForecastEntry entries[FORECAST_ENTRY_COUNT];
  size_t count = 0;
  time_t observedAt = 0;
  bool valid = false;
};

enum class ForecastRequestStatus {
  NotAttempted,
  Loading,
  Available,
  Failed,
};

enum class MainScreen {
  CurrentWeather,
  Forecast,
  EarthquakeHistory,
};

enum class DisplayWakeReason {
  Button,
  Notification,
  ScheduledForecast,
};

enum class WeatherRequestSource {
  Startup,
  ManualButton,
  Scheduled,
};

enum class CloudinessCategory {
  MostlyClear,
  FewClouds,
  Scattered,
  MostlyCloudy,
  Overcast,
  Unknown,
};

WeatherData weather;
ForecastData forecast;
ForecastRequestStatus forecastRequestStatus =
    ForecastRequestStatus::NotAttempted;
MainScreen mainScreen = MainScreen::CurrentWeather;
unsigned long lastForecastInteraction = 0;
unsigned long lastHistoryInteraction = 0;
bool historyDetailShown = false;
bool historyInterruptedBySeismic = false;
unsigned long lastWeatherAttempt = 0;
bool weatherAttempted = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastDisplayActivity = 0;
bool displaySleeping = false;
bool displayDrawingSuppressed = false;
bool japaneseFontReloadPending = false;
bool drawingSuppressedBeforeFontSuspend = false;
unsigned long nextJapaneseFontReloadAttempt = 0;
bool displaySleepEnabled = AppSettings::DEFAULT_DISPLAY_SLEEP_ENABLED;
uint8_t displaySleepMinutes = AppSettings::DEFAULT_DISPLAY_SLEEP_MINUTES;
uint8_t displayBrightnessPercent =
    AppSettings::DEFAULT_DISPLAY_BRIGHTNESS_PERCENT;
unsigned long displayWakePressDetectedAt = 0;
bool displayWakeConfirmationPending = false;
unsigned long buttonAPressDetectedAt = 0;
bool buttonAConfirmationPending = false;
ClockDisplayPrecision clockDisplayPrecision = ClockDisplayPrecision::Minutes;
bool storageAvailable = false;
AppSettings appSettings;
SettingsMode settingsMode;
SpeechService speech;
bool speechAvailable = false;
JapaneseFont japaneseFont;
bool automaticForecastSpeechActive = false;
bool scheduledForecastStopButtonConsumed = false;
TemperatureAlertService temperatureAlerts;
RainAlertService rainAlerts;
RainForecastAlertService rainForecastAlerts;
EarthquakeService earthquakeService;
EarthquakeHistoryService earthquakeHistory;
EarthquakeHistoryReader earthquakeHistoryReader;

struct UpdateNotificationPlan {
  bool higherPriorityTriggered = false;
  bool temperatureAudioRequested = false;
  int temperatureThreshold = 0;
  bool rainAudioRequested = false;
  bool rainForecastTriggered = false;
  bool rainForecastAudioRequested = false;

  void reset() {
    higherPriorityTriggered = false;
    temperatureAudioRequested = false;
    temperatureThreshold = 0;
    rainAudioRequested = false;
    rainForecastTriggered = false;
    rainForecastAudioRequested = false;
  }
};
UpdateNotificationPlan notificationPlan;

AmbientPublisher ambientPublisher;
AmbientPublishResult ambientPublishResult = AmbientPublishResult::NotAttempted;
ThingSpeakPublisher thingSpeakPublisher;
ThingSpeakPublishResult thingSpeakPublishResult =
    ThingSpeakPublishResult::NotAttempted;
WebDownloadServer webDownloadServer;

struct PendingWeatherLog {
  WeatherData data;
  time_t observedAt = 0;
  unsigned long nextRetryAt = 0;
  uint8_t retryCount = 0;
  bool active = false;
};
PendingWeatherLog pendingWeatherLog;

CloudinessCategory classifyCloudiness(int cloudiness) {
  if (cloudiness < 0 || cloudiness > 100) {
    return CloudinessCategory::Unknown;
  }
  if (cloudiness <= 10) return CloudinessCategory::MostlyClear;
  if (cloudiness <= 25) return CloudinessCategory::FewClouds;
  if (cloudiness <= 50) return CloudinessCategory::Scattered;
  if (cloudiness <= 84) return CloudinessCategory::MostlyCloudy;
  return CloudinessCategory::Overcast;
}

const char* cloudinessForDisplay(int cloudiness) {
  switch (classifyCloudiness(cloudiness)) {
    case CloudinessCategory::MostlyClear:
      return "Mostly clear";
    case CloudinessCategory::FewClouds:
      return "Few clouds";
    case CloudinessCategory::Scattered:
      return "Scattered";
    case CloudinessCategory::MostlyCloudy:
      return "Mostly cloudy";
    case CloudinessCategory::Overcast:
      return "Overcast";
    case CloudinessCategory::Unknown:
      return "Clouds";
  }
  return "Clouds";
}

const char* weatherConditionForDisplay(const char* condition, int cloudiness) {
  return strcmp(condition, "Clouds") == 0
             ? cloudinessForDisplay(cloudiness)
             : condition;
}

const char* weatherConditionInJapaneseForDisplay(int conditionId) {
  if (conditionId >= 200 && conditionId <= 299) return "雷雨";
  if (conditionId >= 300 && conditionId <= 399) return "霧雨";
  if (conditionId >= 500 && conditionId <= 599) return "雨";
  if (conditionId >= 600 && conditionId <= 699) return "雪";
  if (conditionId == 701 || conditionId == 741) return "霧";
  if (conditionId == 711) return "煙";
  if (conditionId == 721) return "かすみ";
  if (conditionId == 731 || conditionId == 751 || conditionId == 761) {
    return "砂塵";
  }
  if (conditionId == 762) return "降灰";
  if (conditionId == 771) return "強風";
  if (conditionId == 781) return "竜巻";
  if (conditionId == 800) return "晴れ";
  if (conditionId == 801) return "薄曇り";
  if (conditionId >= 802 && conditionId <= 804) return "曇り";
  return "不明";
}

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

bool writeWeatherLog(const WeatherData& data, time_t observedAt) {
  if (!storageAvailable) {
    return false;
  }

  SdCardGuard sdGuard;
  if (!sdGuard.locked()) {
    Serial.println("SD card is busy; weather log was skipped.");
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
  if (observedAt >= MINIMUM_VALID_TIME) {
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

void scheduleWeatherLogRetry(const WeatherData& data, time_t observedAt) {
  if (pendingWeatherLog.active) {
    Serial.println("Weather log retry is already pending; new retry was skipped.");
    return;
  }
  pendingWeatherLog.data = data;
  pendingWeatherLog.observedAt = observedAt;
  pendingWeatherLog.nextRetryAt = millis() + LOG_RETRY_INTERVAL_MS;
  pendingWeatherLog.retryCount = 0;
  pendingWeatherLog.active = true;
  Serial.println("Weather log retry scheduled in 1 minute.");
}

bool appendWeatherLog(const WeatherData& data, time_t observedAt) {
  if (writeWeatherLog(data, observedAt)) return true;
  scheduleWeatherLogRetry(data, observedAt);
  return false;
}

void processWeatherLogRetry() {
  if (!pendingWeatherLog.active ||
      static_cast<long>(millis() - pendingWeatherLog.nextRetryAt) < 0) return;
  if (writeWeatherLog(pendingWeatherLog.data, pendingWeatherLog.observedAt)) {
    pendingWeatherLog.active = false;
    Serial.println("Weather log retry succeeded.");
    return;
  }
  ++pendingWeatherLog.retryCount;
  if (pendingWeatherLog.retryCount >= LOG_RETRY_LIMIT) {
    pendingWeatherLog.active = false;
    Serial.println("Weather log retry limit reached; pending record discarded.");
    return;
  }
  pendingWeatherLog.nextRetryAt = millis() + LOG_RETRY_INTERVAL_MS;
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
  if (displaySleeping || displayDrawingSuppressed) {
    return;
  }
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
  if (displaySleeping) {
    return;
  }
  M5.Lcd.fillRect(0, 32, 320, 208, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  const int temperatureAlert =
      weather.valid ? temperatureAlerts.activeThreshold(weather.temperature) : 0;
  if (temperatureAlert >= 40) {
    M5.Lcd.fillRect(0, 40, 320, 28, TFT_RED);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_RED);
  } else {
    M5.Lcd.setTextColor(
        temperatureAlert >= 35 ? TFT_RED
                               : temperatureAlert >= 30 ? TFT_ORANGE
                                                        : TFT_CYAN,
        TFT_BLACK);
  }
  M5.Lcd.setCursor(16, 44);
  const bool japanese = japaneseFont.loaded();
  if (japanese && temperatureAlert >= 40) {
    japaneseFont.drawLine(40, "危険な暑さ: 40 ℃", TFT_WHITE, TFT_RED);
  } else if (japanese && temperatureAlert >= 35) {
    japaneseFont.drawLine(40, "高温警戒: 35 ℃", TFT_RED, TFT_BLACK);
  } else if (japanese && temperatureAlert >= 30) {
    japaneseFont.drawLine(40, "高温注意: 30 ℃", TFT_ORANGE, TFT_BLACK);
  } else if (japanese && weather.valid && rainAlerts.isRainActive()) {
    char line[64];
    snprintf(line, sizeof(line), "降雨注意: %.1f mm", weather.rainLastHour);
    japaneseFont.drawLine(40, line, TFT_CYAN, TFT_BLACK);
  } else if (japanese && rainForecastAlerts.isActive()) {
    char line[64];
    snprintf(line, sizeof(line), "降雨予報: %u %%",
             rainForecastAlerts.probabilityPercent());
    japaneseFont.drawLine(40, line, TFT_CYAN, TFT_BLACK);
  } else if (japanese) {
    japaneseFont.drawLine(40, "現在の天気", TFT_CYAN, TFT_BLACK);
  } else if (temperatureAlert >= 40) {
    M5.Lcd.print("EXTREME HEAT: 40 C");
  } else if (temperatureAlert >= 35) {
    M5.Lcd.print("HIGH TEMP WARNING: 35 C");
  } else if (temperatureAlert >= 30) {
    M5.Lcd.print("HIGH TEMP CAUTION: 30 C");
  } else if (weather.valid && rainAlerts.isRainActive()) {
    M5.Lcd.printf("RAIN ALERT: %.1f mm", weather.rainLastHour);
  } else if (rainForecastAlerts.isActive()) {
    M5.Lcd.printf("RAIN FORECAST: %u %%",
                  rainForecastAlerts.probabilityPercent());
  } else {
    M5.Lcd.println("CURRENT WEATHER");
  }
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);

  if (!weather.valid) {
    if (japanese) {
      japaneseFont.drawLine(82, "天気情報なし", TFT_WHITE, TFT_BLACK);
    } else {
      M5.Lcd.setCursor(16, 86);
      M5.Lcd.println("Weather unavailable");
    }
    M5.Lcd.setCursor(16, 214);
    M5.Lcd.setTextSize(1);
    M5.Lcd.println("A:refresh B:speak/stop C:forecast");
    return;
  }

  if (japanese) {
    char line[64];
    snprintf(line, sizeof(line), "天気    : %s",
             weatherConditionInJapaneseForDisplay(weather.conditionId));
    japaneseFont.drawLine(72, line, TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "気温    : %.1f ℃", weather.temperature);
    japaneseFont.drawLine(99, line, TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "湿度    : %d %%", weather.humidity);
    japaneseFont.drawLine(126, line, TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "気圧    : %d hPa", weather.pressure);
    japaneseFont.drawLine(153, line, TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "1時間雨量: %.1f mm", weather.rainLastHour);
    japaneseFont.drawLine(180, line, TFT_WHITE, TFT_BLACK);
  } else {
    M5.Lcd.setCursor(16, 76);
    M5.Lcd.printf("Weather : %s",
                  weatherConditionForDisplay(weather.condition,
                                             weather.cloudiness));
    M5.Lcd.setCursor(16, 104);
    M5.Lcd.printf("Temp    : %.1f C", weather.temperature);
    M5.Lcd.setCursor(16, 132);
    M5.Lcd.printf("Humidity: %d %%", weather.humidity);
    M5.Lcd.setCursor(16, 160);
    M5.Lcd.printf("Pressure: %d hPa", weather.pressure);
    M5.Lcd.setCursor(16, 188);
    M5.Lcd.printf("Rain 1h : %.1f mm", weather.rainLastHour);
  }

  M5.Lcd.setTextSize(1);
  char observedText[20] = "unavailable";
  if (weather.observedAt >= MINIMUM_VALID_TIME) {
    tm observedTime = {};
    localtime_r(&weather.observedAt, &observedTime);
    strftime(observedText, sizeof(observedText), "%Y.%m.%d. %H:%M",
             &observedTime);
  }
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setCursor(16, 207);
  M5.Lcd.printf("API Fetched: %s", observedText);

  const char* ambientStatus = "not attempted";
  uint16_t ambientStatusColor = TFT_LIGHTGREY;
  switch (ambientPublishResult) {
    case AmbientPublishResult::Sent:
      ambientStatus = "sent";
      ambientStatusColor = TFT_GREEN;
      break;
    case AmbientPublishResult::CredentialsMissing:
      ambientStatus = "not configured";
      ambientStatusColor = TFT_ORANGE;
      break;
    case AmbientPublishResult::WiFiDisconnected:
      ambientStatus = "offline";
      ambientStatusColor = TFT_ORANGE;
      break;
    case AmbientPublishResult::TimeUnavailable:
      ambientStatus = "time unavailable";
      ambientStatusColor = TFT_ORANGE;
      break;
    case AmbientPublishResult::RequestFailed:
      ambientStatus = "failed";
      ambientStatusColor = TFT_RED;
      break;
    case AmbientPublishResult::NotAttempted:
      break;
  }
  const char* thingSpeakStatus = "not attempted";
  uint16_t thingSpeakStatusColor = TFT_LIGHTGREY;
  switch (thingSpeakPublishResult) {
    case ThingSpeakPublishResult::Sent:
      thingSpeakStatus = "sent";
      thingSpeakStatusColor = TFT_GREEN;
      break;
    case ThingSpeakPublishResult::CredentialsMissing:
      thingSpeakStatus = "not configured";
      thingSpeakStatusColor = TFT_ORANGE;
      break;
    case ThingSpeakPublishResult::WiFiDisconnected:
      thingSpeakStatus = "offline";
      thingSpeakStatusColor = TFT_ORANGE;
      break;
    case ThingSpeakPublishResult::TimeUnavailable:
      thingSpeakStatus = "time unavailable";
      thingSpeakStatusColor = TFT_ORANGE;
      break;
    case ThingSpeakPublishResult::RequestFailed:
      thingSpeakStatus = "failed";
      thingSpeakStatusColor = TFT_RED;
      break;
    case ThingSpeakPublishResult::NotAttempted:
      break;
  }
  M5.Lcd.setTextColor(
      ambientStatusColor == TFT_RED || thingSpeakStatusColor == TFT_RED
          ? TFT_RED
          : (ambientStatusColor == TFT_GREEN ||
                     thingSpeakStatusColor == TFT_GREEN
                 ? TFT_GREEN
                 : TFT_ORANGE),
      TFT_BLACK);
  M5.Lcd.setCursor(16, 218);
  M5.Lcd.printf("A:%s T:%s", ambientStatus, thingSpeakStatus);

  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(16, 230);
  M5.Lcd.print("A:refresh B:speak/stop C:forecast");
}

void drawForecast() {
  if (displaySleeping) {
    return;
  }
  M5.Lcd.fillRect(0, 32, 320, 208, TFT_BLACK);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(forecastRequestStatus == ForecastRequestStatus::Failed
                          ? TFT_ORANGE
                          : TFT_CYAN,
                      TFT_BLACK);
  M5.Lcd.setCursor(16, 40);
  const bool japanese = japaneseFont.loaded();
  if (rainForecastAlerts.isActive() &&
      forecastRequestStatus != ForecastRequestStatus::Loading) {
    if (japanese) {
      char line[64];
      snprintf(line, sizeof(line), "降雨予報: %u %%",
               rainForecastAlerts.probabilityPercent());
      japaneseFont.drawLine(36, line, TFT_CYAN, TFT_BLACK);
    } else {
      M5.Lcd.printf("RAIN FORECAST: %u %%",
                    rainForecastAlerts.probabilityPercent());
    }
  } else {
    if (japanese) {
      japaneseFont.drawLine(36, "天気予報", TFT_CYAN, TFT_BLACK);
    } else {
      M5.Lcd.print("WEATHER FORECAST");
    }
  }

  M5.Lcd.setTextSize(1);
  if (forecastRequestStatus == ForecastRequestStatus::Loading) {
    M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Lcd.setCursor(226, 42);
    M5.Lcd.print("updating...");
  }

  if (!forecast.valid) {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(forecastRequestStatus == ForecastRequestStatus::Failed
                            ? TFT_RED
                            : TFT_WHITE,
                        TFT_BLACK);
    M5.Lcd.setCursor(16, 104);
    if (japanese) {
      japaneseFont.drawLine(
          100, forecastRequestStatus == ForecastRequestStatus::Failed
                   ? "予報取得失敗"
                   : "予報情報なし",
          forecastRequestStatus == ForecastRequestStatus::Failed ? TFT_RED
                                                                 : TFT_WHITE,
          TFT_BLACK);
    } else {
      M5.Lcd.print(forecastRequestStatus == ForecastRequestStatus::Failed
                       ? "Forecast failed"
                       : "Forecast unavailable");
    }
  } else {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    for (size_t index = 0; index < forecast.count; ++index) {
      const ForecastEntry& entry = forecast.entries[index];
      tm forecastTime = {};
      localtime_r(&entry.forecastAt, &forecastTime);
      const int y = 60 + static_cast<int>(index) * 39;
      if (japanese) {
        char line[64];
        snprintf(line, sizeof(line), "%02d/%02d %02d:%02d %s",
                 forecastTime.tm_mon + 1, forecastTime.tm_mday,
                 forecastTime.tm_hour, forecastTime.tm_min,
                 weatherConditionInJapaneseForDisplay(entry.conditionId));
        japaneseFont.drawLine(y - 3, line, TFT_WHITE, TFT_BLACK);
      } else {
        M5.Lcd.setCursor(16, y);
        M5.Lcd.printf("%02d/%02d %02d:%02d %s", forecastTime.tm_mon + 1,
                      forecastTime.tm_mday, forecastTime.tm_hour,
                      forecastTime.tm_min,
                      weatherConditionForDisplay(entry.condition,
                                                 entry.cloudiness));
      }
      if (japanese) {
        char line[64];
        snprintf(line, sizeof(line), "%.1f ℃/%u %%/%.1f mm",
                 entry.temperature, entry.precipitationProbability,
                 entry.rainThreeHours);
        japaneseFont.drawLine(y + 17, line, TFT_WHITE, TFT_BLACK, 28);
      } else {
        M5.Lcd.setCursor(28, y + 17);
        M5.Lcd.printf("%.1f C/%u %%/%.1f mm", entry.temperature,
                      entry.precipitationProbability, entry.rainThreeHours);
      }
    }
  }

  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setCursor(16, 218);
  if (forecast.valid && forecast.observedAt >= MINIMUM_VALID_TIME) {
    char observedText[20];
    tm observedTime = {};
    localtime_r(&forecast.observedAt, &observedTime);
    strftime(observedText, sizeof(observedText), "%Y.%m.%d. %H:%M",
             &observedTime);
    M5.Lcd.printf("API Fetched: %s", observedText);
  } else {
    M5.Lcd.print("API Fetched: unavailable");
  }

  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(16, 230);
  M5.Lcd.print("A:refresh B:speak/stop C:history");
}

const char* seismicScaleForDisplay(int scale) {
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

void drawSeismicEvent() {
  if (displaySleeping || !earthquakeService.active()) return;
  const SeismicEvent& event = earthquakeService.current();
  M5.Lcd.fillRect(0, 32, 320, 208, TFT_BLACK);
  const bool japanese = japaneseFont.loaded();
  const uint16_t headingColor =
      event.test ? TFT_YELLOW
                 : event.type == SeismicEventType::Eew ? TFT_RED : TFT_ORANGE;
  char line[112];

  if (event.type == SeismicEventType::Eew) {
    snprintf(line, sizeof(line), "%s緊急地震速報 第%d報",
             event.test ? "【試験】" : "", event.serial);
  } else {
    snprintf(line, sizeof(line), "%s地震情報", event.test ? "【試験】" : "");
  }
  if (japanese) {
    japaneseFont.drawLineEllipsized(39, line, headingColor, TFT_BLACK);
  } else {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(headingColor, TFT_BLACK);
    M5.Lcd.setCursor(12, 42);
    M5.Lcd.print(event.type == SeismicEventType::Eew ? "EARTHQUAKE WARNING"
                                                     : "EARTHQUAKE INFO");
  }

  if (event.cancelled) {
    snprintf(line, sizeof(line), "この速報は取り消されました");
  } else {
    snprintf(line, sizeof(line), "対象: %s 最大震度%s", event.targetAreas,
             seismicScaleForDisplay(event.maxScale));
  }
  if (japanese) {
    japaneseFont.drawLineEllipsized(
        77, line, event.cancelled ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    snprintf(line, sizeof(line), "震源: %s", event.hypocenter);
    japaneseFont.drawLineEllipsized(108, line, TFT_WHITE, TFT_BLACK);
    if (event.magnitude >= 0) {
      snprintf(line, sizeof(line), "M %.1f  %s %s", event.magnitude,
               event.type == SeismicEventType::Eew ? "発表" : "発生",
               event.eventTime);
    } else {
      snprintf(line, sizeof(line), "%s %s",
               event.type == SeismicEventType::Eew ? "発表" : "発生",
               event.eventTime);
    }
    japaneseFont.drawLineEllipsized(139, line, TFT_WHITE, TFT_BLACK);
  } else {
    M5.Lcd.setTextSize(2);
    M5.Lcd.setTextColor(event.cancelled ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
    M5.Lcd.setCursor(12, 82);
    M5.Lcd.printf("Scale: %s", seismicScaleForDisplay(event.maxScale));
    M5.Lcd.setCursor(12, 112);
    M5.Lcd.printf("M %.1f  %s", event.magnitude, event.eventTime);
  }
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  M5.Lcd.setCursor(12, 218);
  M5.Lcd.print("P2PQuake realtime information");
}

const char* historyScaleText(int scale) {
  switch (scale) {
    case 0: return "0";
    case 10: return "1";
    case 20: return "2";
    case 30: return "3";
    case 40: return "4";
    case 45: return "5 weak";
    case 46: return "5 weak or more";
    case 50: return "5 strong";
    case 55: return "6 weak";
    case 60: return "6 strong";
    case 70: return "7";
    case 99: return "5 weak or more";
    default: return "-";
  }
}

void formatHistoryTime(const char* value, bool utc, char* output,
                       size_t capacity) {
  tm parsed = {};
  int year, month, day, hour, minute, second;
  if (!value || sscanf(value, "%d%*c%d%*c%d%*c%d:%d:%d", &year, &month,
                        &day, &hour, &minute, &second) != 6) {
    strlcpy(output, "-", capacity);
    return;
  }
  parsed.tm_year = year - 1900;
  if (year < 1970 || year > 2100 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    strlcpy(output, "-", capacity);
    return;
  }
  parsed.tm_mon = month - 1;
  parsed.tm_mday = day;
  parsed.tm_hour = hour;
  parsed.tm_min = minute;
  parsed.tm_sec = second;
  if (utc) {
    const time_t local = mktime(&parsed) + JST_OFFSET_SECONDS;
    localtime_r(&local, &parsed);
  }
  strftime(output, capacity, "%Y.%m.%d %H:%M:%S", &parsed);
}

void drawHistoryAscii(int y, const char* text, uint16_t color = TFT_WHITE) {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(color, TFT_BLACK);
  String fitted(text);
  while (!fitted.isEmpty() && M5.Lcd.textWidth(fitted) > 296) {
    fitted.remove(fitted.length() - 1);
  }
  M5.Lcd.setCursor(12, y);
  M5.Lcd.print(fitted);
}

void drawHistoryJapanese(int y, const char* text) {
  if (japaneseFont.loaded()) {
    japaneseFont.drawLineEllipsized(y, text, TFT_WHITE, TFT_BLACK, 12);
  } else {
    drawHistoryAscii(y, "JP font unavailable", TFT_YELLOW);
  }
}

void drawEarthquakeHistory() {
  M5.Lcd.fillRect(0, 32, 320, 208, TFT_BLACK);
  char line[256];
  snprintf(line, sizeof(line), "HISTORY %s %u / %u",
           historyDetailShown ? "DETAIL" : "",
           earthquakeHistoryReader.count() == 0 ? 0 :
               static_cast<unsigned>(earthquakeHistoryReader.selected() + 1),
           static_cast<unsigned>(earthquakeHistoryReader.count()));
  drawHistoryAscii(39, line, TFT_CYAN);
  if (earthquakeHistoryReader.status() !=
      EarthquakeHistoryReader::Status::Available) {
    const char* message = "History loading...";
    switch (earthquakeHistoryReader.status()) {
      case EarthquakeHistoryReader::Status::Empty:
        message = "No earthquake history"; break;
      case EarthquakeHistoryReader::Status::Unavailable:
        message = "microSD unavailable"; break;
      case EarthquakeHistoryReader::Status::Busy:
        message = "History busy"; break;
      case EarthquakeHistoryReader::Status::Error:
        message = "History read failed"; break;
      default: break;
    }
    drawHistoryAscii(105, message, TFT_YELLOW);
  } else {
    JsonDocument record;
    if (deserializeJson(record, earthquakeHistoryReader.json())) {
      drawHistoryAscii(105, "History read failed", TFT_RED);
    } else {
      const bool eew = strcmp(record["kind"] | "", "eew") == 0;
      const char* correction = record["correction"] | "None";
      const bool corrected = strcmp(correction, "None") != 0 &&
                             strcmp(correction, "Unknown") != 0;
      snprintf(line, sizeof(line), "%s%s%s%s", eew ? "EEW" : "QUAKE",
               (record["test"] | false) ? " TEST" : "",
               (record["cancelled"] | false) ? " CANCEL" : "",
               corrected ? " CORR" : "");
      drawHistoryAscii(53, line, TFT_ORANGE);
      char eventTime[24], issueTime[24], receivedTime[24];
      formatHistoryTime(record["event_time"] | "", false, eventTime,
                        sizeof(eventTime));
      formatHistoryTime(record["issue_time"] | "", false, issueTime,
                        sizeof(issueTime));
      formatHistoryTime(record["received_at"] | "", true, receivedTime,
                        sizeof(receivedTime));
      const float magnitude = record["magnitude"] | -1.0F;
      char magnitudeText[12] = "-";
      if (magnitude >= 0) snprintf(magnitudeText, sizeof(magnitudeText), "%.1f",
                                   magnitude);
      const int scale = eew ? record["max_scale"] | -1
                            : record["target_max_scale"] | -1;
      if (historyDetailShown) {
        snprintf(line, sizeof(line), "Event:    %s", eventTime);
        drawHistoryAscii(67, line);
        snprintf(line, sizeof(line), "Issued:   %s", issueTime);
        drawHistoryAscii(81, line);
        snprintf(line, sizeof(line), "Received: %s JST", receivedTime);
        drawHistoryAscii(95, line);
        snprintf(line, sizeof(line), "Hypocenter: %s", record["hypocenter"] | "-");
        drawHistoryJapanese(110, line);
        snprintf(line, sizeof(line), "M %s  %s scale: %s", magnitudeText,
                 eew ? "Predicted" : "Local", historyScaleText(scale));
        drawHistoryAscii(134, line);
        if (eew) {
          snprintf(line, sizeof(line), "Event ID: %s", record["event_id"] | "-");
          drawHistoryAscii(148, line);
          snprintf(line, sizeof(line), "Report: %d  Target: %s",
                   record["serial"] | 0,
                   (record["target_matched"] | false) ? "Yes" : "No");
          drawHistoryAscii(162, line);
        } else {
          snprintf(line, sizeof(line), "All scale: %s  Type: %s",
                   historyScaleText(record["national_max_scale"] | -1),
                   record["info_type"] | "-");
          drawHistoryAscii(148, line);
          snprintf(line, sizeof(line), "Correction: %s", correction);
          drawHistoryAscii(162, line);
        }
        snprintf(line, sizeof(line), "Areas: %s",
                 eew ? record["target_areas"] | "-"
                     : record["target_prefectures"] | "-");
        drawHistoryJapanese(183, line);
      } else {
        snprintf(line, sizeof(line), "%s: %s", eew ? "Issued" : "Event",
                 eew ? issueTime : eventTime);
        drawHistoryAscii(76, line);
        snprintf(line, sizeof(line), "Received: %s JST", receivedTime);
        drawHistoryAscii(94, line);
        snprintf(line, sizeof(line), "Hypocenter: %s", record["hypocenter"] | "-");
        drawHistoryJapanese(116, line);
        snprintf(line, sizeof(line), "%s scale: %s   M %s",
                 eew ? "Predicted" : "Local", historyScaleText(scale),
                 magnitudeText);
        drawHistoryAscii(146, line);
        snprintf(line, sizeof(line), "Areas: %s",
                 eew ? record["target_areas"] | "-"
                     : record["target_prefectures"] | "-");
        drawHistoryJapanese(171, line);
      }
    }
  }
  M5.Lcd.fillRect(0, 218, 320, 22, TFT_NAVY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_NAVY);
  M5.Lcd.setCursor(24, 225);
  M5.Lcd.print(historyDetailShown ? "Any button: back to history"
                                 : "A:older  B:details  C:weather");
}

void drawMainScreen() {
  if (displaySleeping || displayDrawingSuppressed) {
    return;
  }
  if (earthquakeService.active()) {
    drawSeismicEvent();
    return;
  }
  if (mainScreen == MainScreen::Forecast) {
    drawForecast();
  } else if (mainScreen == MainScreen::EarthquakeHistory) {
    drawEarthquakeHistory();
  } else {
    drawWeather();
  }
}

void noteDisplayActivity() { lastDisplayActivity = millis(); }

unsigned long displaySleepTimeoutMs() {
  return static_cast<unsigned long>(displaySleepMinutes) * 60UL * 1000UL;
}

const char* displayWakeReasonName(DisplayWakeReason reason) {
  switch (reason) {
    case DisplayWakeReason::Button:
      return "button";
    case DisplayWakeReason::Notification:
      return "notification";
    case DisplayWakeReason::ScheduledForecast:
      return "scheduled forecast";
  }
  return "unknown";
}

void wakeDisplay(DisplayWakeReason reason) {
  noteDisplayActivity();
  if (reason == DisplayWakeReason::Button &&
      mainScreen == MainScreen::EarthquakeHistory) {
    lastHistoryInteraction = millis();
  }
  displayWakeConfirmationPending = false;
  if (displaySleeping) {
    M5.Lcd.wakeup();
    M5.Lcd.setBrightness(
        AppSettings::displayBrightnessLevel(displayBrightnessPercent));
    displaySleeping = false;
    Serial.printf("Display woke up (reason: %s).\n",
                  displayWakeReasonName(reason));
  }
  drawDateTime();
  drawMainScreen();
}

void sleepDisplay() {
  if (displaySleeping) {
    return;
  }
  displaySleeping = true;
  M5.Lcd.setBrightness(0);
  M5.Lcd.sleep();
  Serial.println("Display entered sleep mode.");
}

const char* weatherConditionInJapanese(const char* condition, int cloudiness) {
  if (strcmp(condition, "Clear") == 0) return "晴れ";
  if (strcmp(condition, "Clouds") == 0) {
    switch (classifyCloudiness(cloudiness)) {
      case CloudinessCategory::MostlyClear:
        return "ほぼ晴れ";
      case CloudinessCategory::FewClouds:
        return "雲は少なめ";
      case CloudinessCategory::Scattered:
        return "雲がまばら";
      case CloudinessCategory::MostlyCloudy:
        return "雲が多め";
      case CloudinessCategory::Overcast:
      case CloudinessCategory::Unknown:
        return "曇り";
    }
  }
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
           dateTimeText,
           weatherConditionInJapanese(weather.condition, weather.cloudiness),
           weather.temperature, weather.humidity, pressureText, rainText);
  Serial.printf("Speaking: %s\n", message);
  if (!speech.speak(message)) {
    Serial.println("Failed to start speech.");
  }
}

void speakForecast() {
  if (!speechAvailable) {
    Serial.println("Speech service is unavailable.");
    return;
  }
  if (!forecast.valid || forecast.count == 0) {
    speech.speak("予報情報を取得できません。");
    return;
  }

  for (size_t index = 0; index < forecast.count; ++index) {
    const ForecastEntry& entry = forecast.entries[index];
    tm forecastTime = {};
    localtime_r(&entry.forecastAt, &forecastTime);
    char rainPhrase[64] = "";
    if (entry.rainThreeHours > 0.0F) {
      char rainText[24];
      SpeechNumberFormatter::formatOneDecimal(entry.rainThreeHours, rainText,
                                              sizeof(rainText));
      snprintf(rainPhrase, sizeof(rainPhrase), "予想雨量%sミリ。", rainText);
    }

    char message[256];
    snprintf(
        message, sizeof(message),
        "%d月%d日%d時の予報は、%s、気温%.1f度、降水確率%dパーセント。",
        forecastTime.tm_mon + 1, forecastTime.tm_mday, forecastTime.tm_hour,
        weatherConditionInJapanese(entry.condition, entry.cloudiness),
        entry.temperature,
        entry.precipitationProbability);
    strlcat(message, rainPhrase, sizeof(message));

    Serial.printf("Speaking forecast entry %u: %s\n",
                  static_cast<unsigned int>(index + 1), message);
    if (!speech.speak(message)) {
      Serial.println("Failed to start forecast speech.");
      return;
    }

    while (speech.isSpeaking()) {
      M5.update();
      if (M5.BtnB.wasPressed()) {
        lastForecastInteraction = millis();
        speech.stop();
        if (automaticForecastSpeechActive) {
          scheduledForecastStopButtonConsumed = true;
        }
        Serial.println("Forecast speech stopped by button B.");
        return;
      }
      delay(10);
    }
  }
}

uint32_t localDateKey(const tm& localTime) {
  return static_cast<uint32_t>(localTime.tm_year + 1900) * 10000U +
         static_cast<uint32_t>(localTime.tm_mon + 1) * 100U +
         static_cast<uint32_t>(localTime.tm_mday);
}

void runScheduledForecastSpeech() {
  if (earthquakeService.active()) {
    return;
  }
  tm localTime = {};
  if (!getLocalTime(&localTime, 10)) {
    return;
  }

  const uint16_t minuteOfDay =
      static_cast<uint16_t>(localTime.tm_hour * 60 + localTime.tm_min);
  const uint32_t today = localDateKey(localTime);
  bool matched = false;
  for (size_t index = 0; index < AppSettings::FORECAST_SCHEDULE_COUNT;
       ++index) {
    const AppSettings::ForecastSchedule& schedule =
        appSettings.forecastSchedule(index);
    if (!schedule.enabled || schedule.minuteOfDay != minuteOfDay ||
        schedule.lastRunDate == today) {
      continue;
    }
    matched = true;
    appSettings.markForecastScheduleRun(index, today);
  }
  if (!matched) {
    return;
  }

  if (!speechAvailable || !forecast.valid || forecast.count == 0) {
    Serial.println(
        "Scheduled forecast speech skipped because audio or forecast is "
        "unavailable.");
    return;
  }

  automaticForecastSpeechActive = true;
  if (speech.isSpeaking()) {
    speech.stop();
  }
  mainScreen = MainScreen::Forecast;
  lastForecastInteraction = millis();
  wakeDisplay(DisplayWakeReason::ScheduledForecast);
  Serial.printf("Starting scheduled forecast speech for %02d:%02d JST.\n",
                localTime.tm_hour, localTime.tm_min);
  speakForecast();
  automaticForecastSpeechActive = false;
  Serial.println("Scheduled forecast speech finished.");
}

void toggleScreenSpeech() {
  if (speech.isSpeaking()) {
    speech.stop();
    return;
  }
  if (mainScreen == MainScreen::Forecast) {
    speakForecast();
  } else {
    speakCurrentWeather();
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

String buildOpenWeatherUrl(const char* endpoint, const char* language) {
  return String(endpoint) + "?lat=" +
               String(WEATHER_LATITUDE, 6) + "&lon=" +
               String(WEATHER_LONGITUDE, 6) + "&appid=" +
               OPENWEATHER_API_KEY + "&units=metric&lang=" + language;
}

bool fetchCurrentWeather() {
  const String url = buildOpenWeatherUrl(WEATHER_API_URL, "en");

  logRuntimeMemory("current weather before TLS");
  {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, url)) {
      Serial.println("Failed to initialize the weather request.");
      logRuntimeMemory("current weather begin failed");
      return false;
    }

    Serial.println("Requesting current weather...");
    const int statusCode = http.GET();
    logRuntimeMemory("current weather after GET");
    if (statusCode != HTTP_CODE_OK) {
      if (statusCode < 0) {
        Serial.printf("Weather API request failed: %d (%s).\n", statusCode,
                      HTTPClient::errorToString(statusCode).c_str());
      } else {
        Serial.printf("Weather API returned HTTP %d.\n", statusCode);
      }
      http.end();
      logRuntimeMemory("current weather failed after end");
      return false;
    }

    JsonDocument document;
    const DeserializationError error =
        deserializeJson(document, http.getStream());
    if (error) {
      Serial.printf("Weather JSON parsing failed: %s\n", error.c_str());
      http.end();
      logRuntimeMemory("current weather parse failed after end");
      return false;
    }

    strlcpy(weather.condition, document["weather"][0]["main"] | "Unknown",
            sizeof(weather.condition));
    weather.conditionId = document["weather"][0]["id"] | 0;
    weather.cloudiness = document["clouds"]["all"].is<int>()
                             ? document["clouds"]["all"].as<int>()
                             : -1;
    weather.temperature = document["main"]["temp"] | 0.0F;
    weather.humidity = document["main"]["humidity"] | 0;
    weather.pressure = document["main"]["pressure"] | 0;
    weather.rainLastHour = document["rain"]["1h"] | 0.0F;
    weather.observedAt = time(nullptr);
    weather.valid = true;
    http.end();
    logRuntimeMemory("current weather after end");
  }
  logRuntimeMemory("current weather TLS released");

  tm localTime = {};
  const bool timeAvailable = getLocalTime(&localTime, 10);
  const bool quietHours = !timeAvailable || localTime.tm_hour < 6;
  bool temperatureAlertTriggered = false;
  notificationPlan.temperatureAudioRequested = temperatureAlerts.evaluate(
      weather.temperature, speechAvailable && !quietHours,
      &temperatureAlertTriggered, &notificationPlan.temperatureThreshold);
  bool rainAlertTriggered = rainAlerts.evaluate(
      isRainingCondition(weather.condition), weather.condition,
      weather.rainLastHour,
      speechAvailable && !quietHours &&
          !notificationPlan.temperatureAudioRequested,
      &notificationPlan.rainAudioRequested);
  notificationPlan.higherPriorityTriggered =
      temperatureAlertTriggered || rainAlertTriggered;

  Serial.printf(
      "Weather updated: %s (%d), cloudiness %d %%, %.1f C, %d %%, %d hPa, "
      "%.1f mm/h\n",
      weather.condition, weather.conditionId, weather.cloudiness,
      weather.temperature,
      weather.humidity, weather.pressure, weather.rainLastHour);
  appendWeatherLog(weather, weather.observedAt);
  ambientPublishResult = ambientPublisher.publish(
      weather.observedAt, weather.temperature,
      weather.humidity, weather.pressure, weather.rainLastHour,
      temperatureAlerts.activeThreshold(weather.temperature),
      isRainingCondition(weather.condition), WiFi.RSSI(), weather.conditionId);
  drawMainScreen();
  return true;
}

void markForecastRequestFailed() {
  forecastRequestStatus = ForecastRequestStatus::Failed;
  if (mainScreen == MainScreen::Forecast) {
    drawForecast();
  }
}

bool fetchForecast() {
  forecastRequestStatus = ForecastRequestStatus::Loading;
  if (mainScreen == MainScreen::Forecast) {
    drawForecast();
  }

  const String url =
      buildOpenWeatherUrl(FORECAST_API_URL, "ja") + "&cnt=" +
      String(static_cast<unsigned int>(FORECAST_ENTRY_COUNT));
  logRuntimeMemory("forecast before TLS");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    Serial.println("Failed to initialize the forecast request.");
    logRuntimeMemory("forecast begin failed");
    markForecastRequestFailed();
    return false;
  }

  Serial.println("Requesting forecast...");
  const int statusCode = http.GET();
  logRuntimeMemory("forecast after GET");
  if (statusCode != HTTP_CODE_OK) {
    if (statusCode < 0) {
      Serial.printf("Forecast API request failed: %d (%s).\n", statusCode,
                    HTTPClient::errorToString(statusCode).c_str());
    } else {
      Serial.printf("Forecast API returned HTTP %d.\n", statusCode);
    }
    http.end();
    logRuntimeMemory("forecast failed after end");
    markForecastRequestFailed();
    return false;
  }

  JsonDocument filter;
  for (size_t index = 0; index < FORECAST_ENTRY_COUNT; ++index) {
    filter["list"][index]["dt"] = true;
    filter["list"][index]["main"]["temp"] = true;
    filter["list"][index]["weather"][0]["main"] = true;
    filter["list"][index]["weather"][0]["id"] = true;
    filter["list"][index]["clouds"]["all"] = true;
    filter["list"][index]["pop"] = true;
    filter["list"][index]["rain"]["3h"] = true;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(
      document, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  logRuntimeMemory("forecast after end");
  if (error) {
    Serial.printf("Forecast JSON parsing failed: %s\n", error.c_str());
    markForecastRequestFailed();
    return false;
  }

  ForecastData updated;
  const JsonArrayConst entries = document["list"].as<JsonArrayConst>();
  for (JsonObjectConst source : entries) {
    if (updated.count >= FORECAST_ENTRY_COUNT) {
      break;
    }
    const time_t forecastAt =
        static_cast<time_t>(source["dt"] | static_cast<int64_t>(0));
    if (forecastAt < MINIMUM_VALID_TIME) {
      continue;
    }

    ForecastEntry& entry = updated.entries[updated.count++];
    entry.forecastAt = forecastAt;
    strlcpy(entry.condition, source["weather"][0]["main"] | "Unknown",
            sizeof(entry.condition));
    entry.conditionId = source["weather"][0]["id"] | 0;
    entry.cloudiness =
        source["clouds"]["all"].is<int>() ? source["clouds"]["all"].as<int>()
                                           : -1;
    entry.temperature = source["main"]["temp"] | 0.0F;
    const float probability = source["pop"] | 0.0F;
    entry.precipitationProbability = static_cast<uint8_t>(
        constrain(static_cast<int>(probability * 100.0F + 0.5F), 0, 100));
    entry.rainThreeHours = source["rain"]["3h"] | 0.0F;
  }

  if (updated.count == 0) {
    Serial.println("Forecast response contained no usable entries.");
    markForecastRequestFailed();
    return false;
  }

  updated.observedAt = time(nullptr);
  updated.valid = true;
  forecast = updated;
  forecastRequestStatus = ForecastRequestStatus::Available;
  Serial.printf("Forecast updated with %u entries.\n",
                static_cast<unsigned int>(forecast.count));
  for (size_t index = 0; index < forecast.count; ++index) {
    const ForecastEntry& entry = forecast.entries[index];
    Serial.printf(
        "  %lld: %s, cloudiness %d %%, %.1f C, PoP %u %%, rain %.1f "
        "mm/3h\n",
        static_cast<long long>(entry.forecastAt), entry.condition,
        entry.cloudiness, entry.temperature, entry.precipitationProbability,
        entry.rainThreeHours);
  }

  const ForecastEntry& nearest = forecast.entries[0];
  constexpr uint8_t RAIN_FORECAST_PROBABILITY_THRESHOLD = 50;
  constexpr float RAIN_FORECAST_AMOUNT_THRESHOLD_MM = 0.1F;
  const bool forecastMatches =
      nearest.precipitationProbability >=
          RAIN_FORECAST_PROBABILITY_THRESHOLD &&
      nearest.rainThreeHours >= RAIN_FORECAST_AMOUNT_THRESHOLD_MM;
  tm localTime = {};
  const bool timeAvailable = getLocalTime(&localTime, 10);
  const bool quietHours = !timeAvailable || localTime.tm_hour < 6;
  const bool rainingNow =
      weather.valid && isRainingCondition(weather.condition);
  notificationPlan.rainForecastTriggered = rainForecastAlerts.evaluate(
      forecastMatches, rainingNow, nearest.forecastAt,
      nearest.precipitationProbability, nearest.rainThreeHours,
      speechAvailable && !quietHours &&
          !notificationPlan.higherPriorityTriggered && !speech.isSpeaking(),
      &notificationPlan.rainForecastAudioRequested);
  return true;
}

void applyNotificationPlan() {
  if (earthquakeService.active()) {
    if (notificationPlan.higherPriorityTriggered ||
        notificationPlan.rainForecastTriggered) {
      Serial.println(
          "Weather notification audio suppressed by seismic information.");
    }
    return;
  }
  if (notificationPlan.higherPriorityTriggered) {
    mainScreen = MainScreen::CurrentWeather;
  } else if (notificationPlan.rainForecastTriggered) {
    mainScreen = MainScreen::Forecast;
    lastForecastInteraction = millis();
  } else {
    return;
  }

  wakeDisplay(DisplayWakeReason::Notification);

  if (notificationPlan.temperatureAudioRequested) {
    temperatureAlerts.notify(weather.temperature,
                             notificationPlan.temperatureThreshold, speech);
  } else if (notificationPlan.rainAudioRequested) {
    rainAlerts.notify(weather.rainLastHour, speech);
  } else if (notificationPlan.rainForecastAudioRequested &&
             forecast.count > 0) {
    rainForecastAlerts.notify(forecast.entries[0].precipitationProbability,
                              forecast.entries[0].rainThreeHours, speech);
  }
}

bool updateWeather(WeatherRequestSource source,
                   bool revealDisplayAfterFetch = false) {
  if (automaticForecastSpeechActive) {
    Serial.println(
        "Weather update deferred during scheduled forecast speech.");
    return false;
  }
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
  logRuntimeMemory("weather update start");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Weather update skipped because Wi-Fi is disconnected.");
    markForecastRequestFailed();
    drawMainScreen();
    return false;
  }

  const bool earthquakeConnectionPaused =
      earthquakeService.pauseForNetworkRequest();
  const bool japaneseFontSuspended = japaneseFont.suspendForNetworkRequest();
  const bool drawingWasSuppressed = displayDrawingSuppressed;
  if (japaneseFontSuspended) displayDrawingSuppressed = true;
  notificationPlan.reset();
  const bool currentUpdated = fetchCurrentWeather();
  Serial.printf("Current weather request result: %s.\n",
                currentUpdated ? "success" : "failed");
  logRuntimeMemory("after current weather and Ambient");
  const bool forecastUpdated = fetchForecast();
  Serial.printf("Forecast request result: %s.\n",
                forecastUpdated ? "success" : "failed");
  logRuntimeMemory("after forecast");
  if (revealDisplayAfterFetch) {
    // Keep drawing suppressed while the SD-backed font is unloaded. The
    // completed screen is drawn after a successful reload below.
    if (!japaneseFontSuspended && !japaneseFontReloadPending) {
      displayDrawingSuppressed = false;
      drawDateTime();
      drawMainScreen();
    }
  }
  if (currentUpdated && forecastUpdated && weather.valid && forecast.valid &&
      forecast.count > 0) {
    logRuntimeMemory("before ThingSpeak publish");
    thingSpeakPublishResult = thingSpeakPublisher.publish(
        weather.observedAt, weather.temperature, weather.humidity,
        weather.pressure, weather.conditionId,
        forecast.entries[0].precipitationProbability,
        temperatureAlerts.activeThreshold(weather.temperature), WiFi.RSSI(),
        rainAlerts.isRainActive());
    logRuntimeMemory("after ThingSpeak publish");
  } else {
    Serial.printf(
        "ThingSpeak publish skipped: current=%s, forecast=%s, "
        "weatherValid=%s, forecastValid=%s, forecastCount=%u.\n",
        currentUpdated ? "success" : "failed",
        forecastUpdated ? "success" : "failed", weather.valid ? "yes" : "no",
        forecast.valid ? "yes" : "no",
        static_cast<unsigned>(forecast.count));
  }
  if (earthquakeConnectionPaused) {
    earthquakeService.resumeAfterNetworkRequest();
  }
  if (japaneseFontSuspended) {
    if (japaneseFont.resumeAfterNetworkRequest()) {
      displayDrawingSuppressed = drawingWasSuppressed;
    } else {
      japaneseFontReloadPending = true;
      drawingSuppressedBeforeFontSuspend = drawingWasSuppressed;
      nextJapaneseFontReloadAttempt = millis() + JAPANESE_FONT_RELOAD_RETRY_MS;
      Serial.println("Japanese font reload will be retried from loop().");
    }
  }
  applyNotificationPlan();
  logRuntimeMemory("weather update complete");
  drawMainScreen();
  return currentUpdated || forecastUpdated;
}

void retryJapaneseFontReload() {
  if (!japaneseFontReloadPending ||
      static_cast<long>(millis() - nextJapaneseFontReloadAttempt) < 0) {
    return;
  }
  if (!japaneseFont.resumeAfterNetworkRequest()) {
    nextJapaneseFontReloadAttempt = millis() + JAPANESE_FONT_RELOAD_RETRY_MS;
    return;
  }
  japaneseFontReloadPending = false;
  displayDrawingSuppressed = drawingSuppressedBeforeFontSuspend;
  Serial.println("Japanese font reload retry succeeded.");
  drawDateTime();
  drawMainScreen();
}
}  // namespace

void setup() {
  // Initialize the display and serial port here; SD is initialized separately
  // so that card detection and errors can be handled explicitly.
  M5.begin(true, false, true);
  Serial.begin(115200);
  Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Firmware commit date: %s\n", FIRMWARE_COMMIT_DATE);
  appSettings.begin();
  clockDisplayPrecision = appSettings.clockPrecision();
  displaySleepEnabled = appSettings.displaySleepEnabled();
  displaySleepMinutes = appSettings.displaySleepMinutes();
  displayBrightnessPercent = appSettings.displayBrightnessPercent();
  M5.Lcd.setBrightness(
      AppSettings::displayBrightnessLevel(displayBrightnessPercent));
  noteDisplayActivity();
  speech.setVolumePercent(appSettings.volumePercent());
  const bool settingsRequested = showSplashScreen();

  storageAvailable = initializeStorage();
  speechAvailable = storageAvailable && speech.begin();
  temperatureAlerts.begin();
  rainAlerts.begin();
  rainForecastAlerts.begin();
  connectToWiFi();
  earthquakeHistory.begin(storageAvailable);
  earthquakeHistoryReader.begin(&earthquakeHistory, storageAvailable);
  webDownloadServer.begin(storageAvailable, &earthquakeHistory);
  syncTimeWithNtp();

  earthquakeService.begin(
      EARTHQUAKE_TARGET_PREFECTURES,
      sizeof(EARTHQUAKE_TARGET_PREFECTURES) /
          sizeof(EARTHQUAKE_TARGET_PREFECTURES[0]),
      EARTHQUAKE_USE_SANDBOX, EARTHQUAKE_ALLOW_SANDBOX_AUDIO,
      &earthquakeHistory);

  displayDrawingSuppressed = true;
  updateWeather(WeatherRequestSource::Startup, !settingsRequested);
  displayDrawingSuppressed = false;

  japaneseFont.begin(storageAvailable);

  if (settingsRequested) {
    tm diagnosticTime = {};
    const DiagnosticStatus diagnostics = {
        storageAvailable,
        storageAvailable && SD.exists("/aq_dic/aqdic_m.bin"),
        speechAvailable,
        japaneseFont.loaded(),
        WiFi.status() == WL_CONNECTED,
        getLocalTime(&diagnosticTime, 10),
        weather.valid,
        WiFi.localIP(),
    };
    settingsMode.run(appSettings, speech, speechAvailable, diagnostics);
    clockDisplayPrecision = appSettings.clockPrecision();
    displaySleepEnabled = appSettings.displaySleepEnabled();
    displaySleepMinutes = appSettings.displaySleepMinutes();
    displayBrightnessPercent = appSettings.displayBrightnessPercent();
    M5.Lcd.setBrightness(
        AppSettings::displayBrightnessLevel(displayBrightnessPercent));
    speech.setVolumePercent(appSettings.volumePercent());
    noteDisplayActivity();
  }

  drawDateTime();
  drawMainScreen();
}

void loop() {
  M5.update();
  retryJapaneseFontReload();
  earthquakeService.loop();
  earthquakeHistory.loop();
  if (mainScreen == MainScreen::EarthquakeHistory) {
    if (earthquakeService.active()) {
      historyInterruptedBySeismic = true;
    } else if (historyInterruptedBySeismic) {
      historyInterruptedBySeismic = false;
      historyDetailShown = false;
      lastHistoryInteraction = millis();
      earthquakeHistoryReader.refresh();
    }
    if (!earthquakeService.active()) {
      earthquakeHistoryReader.loop();
      if (earthquakeHistoryReader.consumeChanged()) drawMainScreen();
    }
  }
  webDownloadServer.handleClient();
  thingSpeakPublisher.handle();
  processWeatherLogRetry();
  temperatureAlerts.processPendingLogs();
  rainAlerts.processPendingLog();
  rainForecastAlerts.processPendingLog();

  if (earthquakeService.consumeWakeRequested()) {
    wakeDisplay(DisplayWakeReason::Notification);
  }
  const SeismicSoundType seismicSound =
      earthquakeService.consumeSoundRequested();
  if (seismicSound != SeismicSoundType::None) {
    if (speechAvailable) {
      speech.playAlertTone(180,
                           seismicSound == SeismicSoundType::EewWarning ? 3 : 1);
    } else {
      Serial.println("Seismic alert tone skipped because audio is unavailable.");
    }
  }
  if (earthquakeService.consumeDisplayChanged()) {
    drawDateTime();
    drawMainScreen();
  }

  if (displaySleeping) {
    const bool wakeButtonPressed =
        M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnC.isPressed();
    if (!displayWakeConfirmationPending && wakeButtonPressed) {
      displayWakeConfirmationPending = true;
      displayWakePressDetectedAt = millis();
      Serial.printf("Display wake signal detected (A:%d B:%d C:%d).\n",
                    M5.BtnA.isPressed(), M5.BtnB.isPressed(),
                    M5.BtnC.isPressed());
    }
    if (displayWakeConfirmationPending) {
      if (!wakeButtonPressed) {
        displayWakeConfirmationPending = false;
        Serial.println("Display wake signal rejected as too short.");
      } else if (millis() - displayWakePressDetectedAt >=
                 BUTTON_CONFIRMATION_MS) {
        buttonAConfirmationPending = false;
        Serial.println("Display wake button press confirmed.");
        wakeDisplay(DisplayWakeReason::Button);
        return;
      }
    }
  }

  runScheduledForecastSpeech();

  const bool seismicDisplayActive = earthquakeService.active();
  if (seismicDisplayActive) buttonAConfirmationPending = false;
  if (seismicDisplayActive &&
      (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed())) {
    noteDisplayActivity();
    buttonAConfirmationPending = false;
    Serial.println("Button press ignored while seismic information is shown.");
  }
  const bool historyDetailReturned = !displaySleeping &&
      !seismicDisplayActive && mainScreen == MainScreen::EarthquakeHistory &&
      historyDetailShown &&
      (M5.BtnA.wasPressed() || M5.BtnB.wasPressed() || M5.BtnC.wasPressed());
  if (historyDetailReturned) {
    historyDetailShown = false;
    buttonAConfirmationPending = false;
    lastHistoryInteraction = millis();
    noteDisplayActivity();
    drawMainScreen();
  }
  if (!historyDetailReturned && !displaySleeping && !seismicDisplayActive &&
      M5.BtnA.wasPressed()) {
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
      noteDisplayActivity();
      if (mainScreen == MainScreen::Forecast) {
        lastForecastInteraction = millis();
      }
      if (mainScreen == MainScreen::EarthquakeHistory) {
        lastHistoryInteraction = millis();
        earthquakeHistoryReader.next();
        drawMainScreen();
      } else {
        updateWeather(WeatherRequestSource::ManualButton);
      }
    }
  }
  if (scheduledForecastStopButtonConsumed) {
    scheduledForecastStopButtonConsumed = false;
  } else if (!historyDetailReturned && !displaySleeping && !seismicDisplayActive &&
             M5.BtnB.wasPressed()) {
    noteDisplayActivity();
    if (mainScreen == MainScreen::Forecast) {
      lastForecastInteraction = millis();
    }
    if (mainScreen == MainScreen::EarthquakeHistory) {
      lastHistoryInteraction = millis();
      historyDetailShown = earthquakeHistoryReader.status() ==
          EarthquakeHistoryReader::Status::Available;
      buttonAConfirmationPending = false;
      drawMainScreen();
    } else {
      toggleScreenSpeech();
    }
  }
  if (!historyDetailReturned && !displaySleeping && !seismicDisplayActive &&
      M5.BtnC.wasPressed()) {
    noteDisplayActivity();
    buttonAConfirmationPending = false;
    if (mainScreen == MainScreen::CurrentWeather) {
      mainScreen = MainScreen::Forecast;
    } else if (mainScreen == MainScreen::Forecast) {
      mainScreen = MainScreen::EarthquakeHistory;
      historyDetailShown = false;
      historyInterruptedBySeismic = false;
      lastHistoryInteraction = millis();
      earthquakeHistoryReader.refresh();
    } else {
      mainScreen = MainScreen::CurrentWeather;
      earthquakeHistoryReader.close();
    }
    if (mainScreen == MainScreen::Forecast) {
      lastForecastInteraction = millis();
    }
    drawMainScreen();
  }

  const unsigned long now = millis();
  if (now - lastWeatherAttempt >= WEATHER_UPDATE_INTERVAL_MS) {
    updateWeather(WeatherRequestSource::Scheduled);
  }
  if (mainScreen == MainScreen::Forecast &&
      now - lastForecastInteraction >= FORECAST_SCREEN_TIMEOUT_MS) {
    mainScreen = MainScreen::CurrentWeather;
    Serial.println("Forecast screen timed out; returning to current weather.");
    drawMainScreen();
  }
  if (mainScreen == MainScreen::EarthquakeHistory && !displaySleeping &&
      !seismicDisplayActive &&
      millis() - lastHistoryInteraction >= FORECAST_SCREEN_TIMEOUT_MS) {
    mainScreen = MainScreen::CurrentWeather;
    historyDetailShown = false;
    earthquakeHistoryReader.close();
    Serial.println("History screen timed out; returning to current weather.");
    drawMainScreen();
  }
  if (now - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL_MS) {
    lastDisplayUpdate = now;
    drawDateTime();
  }
  if (displaySleepEnabled && !displaySleeping &&
      now - lastDisplayActivity >= displaySleepTimeoutMs()) {
    sleepDisplay();
  }
}
