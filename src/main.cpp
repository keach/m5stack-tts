#include <Arduino.h>
#include <M5Stack.h>
#include <WiFi.h>

#include "secrets.h"

namespace {
constexpr unsigned long WIFI_TIMEOUT_MS = 20000;

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
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(20, 40);
    M5.Lcd.println("Button A pressed");
  }
}
