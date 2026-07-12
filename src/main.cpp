#include <Arduino.h>
#include <M5Stack.h>

void setup() {
  M5.begin();
  Serial.begin(115200);

  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.setCursor(20, 40);
  M5.Lcd.println("M5Stack Basic");
  M5.Lcd.setCursor(20, 70);
  M5.Lcd.println("PlatformIO ready!");
}

void loop() {
  M5.update();

  if (M5.BtnA.wasPressed()) {
    M5.Lcd.fillScreen(TFT_BLACK);
    M5.Lcd.setCursor(20, 40);
    M5.Lcd.println("Button A pressed");
  }
}

