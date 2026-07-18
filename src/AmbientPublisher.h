#pragma once

#include <Arduino.h>
#include <time.h>

class AmbientPublisher {
 public:
  bool publish(time_t observedAt, const char* condition, float temperature,
               int humidity, int pressure, float rainLastHour,
               int temperatureAlertThreshold, bool rainingNow, int wifiRssi);
};
