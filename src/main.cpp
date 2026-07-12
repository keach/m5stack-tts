#include <Arduino.h>
#include <M5Stack.h>
#include <WiFi.h>
#include <time.h>

#include "secrets.h"

namespace {
constexpr unsigned long WIFI_TIMEOUT_MS = 20000;
constexpr long JST_OFFSET_SECONDS = 9 * 60 * 60;
constexpr int DAYLIGHT_OFFSET_SECONDS = 0;
constexpr unsigned long NTP_TIMEOUT_MS = 15000;
constexpr char NTP_SERVER_PRIMARY[] = "ntp.nict.jp";
constexpr char NTP_SERVER_SECONDARY[] = "pool.ntp.org";

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
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(20, 40);
    M5.Lcd.println("Button A pressed");
  }
}
