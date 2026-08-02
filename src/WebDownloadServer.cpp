#include "WebDownloadServer.h"

#include <SD.h>
#include <WiFi.h>
#include "SdCardLock.h"

namespace {
constexpr WebDownloadServer::DownloadFile DOWNLOADS[] = {
    {"Weather log", "/weather.csv", "/download/weather", "text/csv; charset=utf-8", "weather.csv"},
    {"Temperature alerts", "/temperature_alerts.csv", "/download/temperature-alerts", "text/csv; charset=utf-8", "temperature_alerts.csv"},
    {"Rain alerts", "/rain_alerts.csv", "/download/rain-alerts", "text/csv; charset=utf-8", "rain_alerts.csv"},
    {"Ambient queue", "/ambient_queue.ndjson", "/download/ambient-queue", "application/x-ndjson", "ambient_queue.ndjson"},
    {"Rain forecast alerts", "/rain_forecast_alerts.csv", "/download/rain-forecast-alerts", "text/csv; charset=utf-8", "rain_forecast_alerts.csv"},
};
constexpr size_t DOWNLOAD_COUNT = sizeof(DOWNLOADS) / sizeof(DOWNLOADS[0]);
}

void WebDownloadServer::begin(bool storageAvailable) {
  storageAvailable_ = storageAvailable;
  registerRoutes();
  startIfReady();
}
void WebDownloadServer::registerRoutes() {
  if (routesRegistered_) return;
  server_.on("/", HTTP_GET, [this]() { sendIndex(); });
  for (size_t index = 0; index < DOWNLOAD_COUNT; ++index) {
    server_.on(DOWNLOADS[index].route, HTTP_GET,
               [this, index]() { sendDownload(DOWNLOADS[index]); });
  }
  server_.onNotFound([this]() { sendText(404, "Not Found"); });
  routesRegistered_ = true;
}
void WebDownloadServer::startIfReady() {
  if (started_ || WiFi.status() != WL_CONNECTED) return;
  registerRoutes();
  server_.begin();
  started_ = true;
  Serial.printf("Web download server started at http://%s/\n",
                WiFi.localIP().toString().c_str());
}
void WebDownloadServer::handleClient() {
  startIfReady();
  if (started_ && WiFi.status() == WL_CONNECTED) server_.handleClient();
}
void WebDownloadServer::sendText(int status, const char* message) {
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(status, "text/plain; charset=utf-8", message);
}
void WebDownloadServer::sendIndex() {
  if (!storageAvailable_) { sendText(503, "microSD is unavailable"); return; }
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) { sendText(503, "microSD is busy"); return; }
  String html;
  html.reserve(1400);
  html = F("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>M5 Weather Data</title></head><body><h1>M5 Weather Data</h1><ul>");
  for (const DownloadFile& download : DOWNLOADS) {
    html += F("<li>"); html += download.label; html += F(" ("); html += download.path; html += F("): ");
    if (SD.exists(download.path)) {
      File file = SD.open(download.path, FILE_READ);
      if (file) {
        html += String(static_cast<unsigned long>(file.size()));
        html += F(" bytes - <a href=\""); html += download.route; html += F("\">Download</a>");
        file.close();
      } else html += F("Unavailable");
    } else html += F("Not created yet");
    html += F("</li>");
  }
  html += F("</ul></body></html>");
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "text/html; charset=utf-8", html);
}
void WebDownloadServer::sendDownload(const DownloadFile& download) {
  if (!storageAvailable_) { sendText(503, "microSD is unavailable"); return; }
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) { sendText(503, "microSD is busy"); return; }
  if (!SD.exists(download.path)) { sendText(404, "File not created yet"); return; }
  File file = SD.open(download.path, FILE_READ);
  if (!file) { sendText(503, "Unable to open file"); return; }
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("Content-Disposition",
                     String("attachment; filename=\"") + download.downloadName + "\"");
  server_.streamFile(file, download.contentType);
  file.close();
}
