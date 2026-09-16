#include "WebDownloadServer.h"

#include <SD.h>
#include <WiFi.h>
#include "EarthquakeHistoryService.h"
#include "EarthquakeService.h"
#include "SdCardLock.h"
#include "SpeechService.h"
#include "JapaneseFont.h"

namespace {
constexpr WebDownloadServer::DownloadFile DOWNLOADS[] = {
    {"Weather log", "/weather.csv", "/download/weather", "text/csv; charset=utf-8", "weather.csv"},
    {"Temperature alerts", "/temperature_alerts.csv", "/download/temperature-alerts", "text/csv; charset=utf-8", "temperature_alerts.csv"},
    {"Rain alerts", "/rain_alerts.csv", "/download/rain-alerts", "text/csv; charset=utf-8", "rain_alerts.csv"},
    {"Ambient queue", "/ambient_queue.ndjson", "/download/ambient-queue", "application/x-ndjson", "ambient_queue.ndjson"},
    {"ThingSpeak queue", "/thingspeak_queue.ndjson", "/download/thingspeak-queue", "application/x-ndjson", "thingspeak_queue.ndjson"},
    {"Rain forecast alerts", "/rain_forecast_alerts.csv", "/download/rain-forecast-alerts", "text/csv; charset=utf-8", "rain_forecast_alerts.csv"},
};
constexpr size_t DOWNLOAD_COUNT = sizeof(DOWNLOADS) / sizeof(DOWNLOADS[0]);

class P2PQuakeNetworkGuard {
 public:
  explicit P2PQuakeNetworkGuard(EarthquakeService* earthquakeService,
                                JapaneseFont* japaneseFont,
                                bool* fontReloadPending)
      : earthquakeService_(earthquakeService),
        japaneseFont_(japaneseFont),
        fontReloadPending_(fontReloadPending),
        paused_(earthquakeService_ &&
                earthquakeService_->pauseForNetworkRequest()),
        fontSuspended_(japaneseFont_ &&
                       japaneseFont_->suspendForNetworkRequest()) {}

  ~P2PQuakeNetworkGuard() {
    if (fontSuspended_ && !japaneseFont_->resumeAfterNetworkRequest()) {
      *fontReloadPending_ = true;
    }
    if (paused_) earthquakeService_->resumeAfterNetworkRequest();
  }

 private:
  EarthquakeService* earthquakeService_;
  JapaneseFont* japaneseFont_;
  bool* fontReloadPending_;
  bool paused_;
  bool fontSuspended_;
};
}

void WebDownloadServer::begin(
    bool storageAvailable, EarthquakeHistoryService* earthquakeHistory,
    EarthquakeService* earthquakeService, SpeechService* speechService,
    JapaneseFont* japaneseFont) {
  storageAvailable_ = storageAvailable;
  earthquakeHistory_ = earthquakeHistory;
  earthquakeService_ = earthquakeService;
  speechService_ = speechService;
  japaneseFont_ = japaneseFont;
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
  server_.on("/download/earthquake-history", HTTP_GET,
             [this]() { sendEarthquakeHistoryDownload(); });
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
bool WebDownloadServer::consumeJapaneseFontReloadPending() {
  const bool pending = japaneseFontReloadPending_;
  japaneseFontReloadPending_ = false;
  return pending;
}
bool WebDownloadServer::sendSpeechBusy() {
  if (!speechService_ || !speechService_->isSpeaking()) return false;
  server_.send(503, "text/plain; charset=utf-8", "Speech in progress. Retry shortly.");
  return true;
}
void WebDownloadServer::sendText(int status, const char* message) {
  if (sendSpeechBusy()) return;
  P2PQuakeNetworkGuard networkGuard(earthquakeService_, japaneseFont_,
                                    &japaneseFontReloadPending_);
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(status, "text/plain; charset=utf-8", message);
}
void WebDownloadServer::sendIndex() {
  if (sendSpeechBusy()) return;
  P2PQuakeNetworkGuard networkGuard(earthquakeService_, japaneseFont_,
                                    &japaneseFontReloadPending_);
  if (!storageAvailable_) { sendText(503, "microSD is unavailable"); return; }
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) { sendText(503, "microSD is busy"); return; }
  String html;
  html.reserve(8192);
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
  html += F("</ul>");
  if (earthquakeHistory_) earthquakeHistory_->appendWebSectionLocked(html);
  html += F("</body></html>");
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Cache-Control", "no-store");
  server_.send(200, "text/html; charset=utf-8", html);
}
void WebDownloadServer::sendDownload(const DownloadFile& download) {
  if (sendSpeechBusy()) return;
  P2PQuakeNetworkGuard networkGuard(earthquakeService_, japaneseFont_,
                                    &japaneseFontReloadPending_);
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

void WebDownloadServer::sendEarthquakeHistoryDownload() {
  if (sendSpeechBusy()) return;
  P2PQuakeNetworkGuard networkGuard(earthquakeService_, japaneseFont_,
                                    &japaneseFontReloadPending_);
  if (!storageAvailable_ || !earthquakeHistory_) {
    sendText(503, "Earthquake history is unavailable");
    return;
  }
  const String type = server_.arg("type");
  const String generationText = server_.arg("generation");
  if ((type != "eew" && type != "earthquake") ||
      generationText.length() != 6) {
    sendText(400, "Invalid history download parameters");
    return;
  }
  uint32_t generation = 0;
  for (size_t index = 0; index < generationText.length(); ++index) {
    const char value = generationText[index];
    if (value < '0' || value > '9') {
      sendText(400, "Invalid history generation");
      return;
    }
    generation = generation * 10 + static_cast<uint32_t>(value - '0');
  }
  const EarthquakeHistoryKind kind =
      type == "eew" ? EarthquakeHistoryKind::Eew
                    : EarthquakeHistoryKind::Earthquake;
  char path[48];
  if (!earthquakeHistory_->resolvePath(kind, generation, path,
                                       sizeof(path))) {
    sendText(404, "History generation not found");
    return;
  }
  SdCardGuard sdGuard;
  if (!sdGuard.locked()) {
    sendText(503, "microSD is busy");
    return;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    sendText(503, "Unable to open history file");
    return;
  }
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader(
      "Content-Disposition",
      String("attachment; filename=\"") + (path[0] == '/' ? path + 1 : path) +
          "\"");
  server_.streamFile(file, "application/x-ndjson; charset=utf-8");
  file.close();
}
