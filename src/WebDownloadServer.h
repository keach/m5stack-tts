#pragma once
#include <WebServer.h>

class EarthquakeHistoryService;
class EarthquakeService;
class SpeechService;
class JapaneseFont;

class WebDownloadServer {
 public:
  struct DownloadFile {
    const char* label;
    const char* path;
    const char* route;
    const char* contentType;
    const char* downloadName;
  };
  void begin(bool storageAvailable,
             EarthquakeHistoryService* earthquakeHistory = nullptr,
             EarthquakeService* earthquakeService = nullptr,
             SpeechService* speechService = nullptr,
             JapaneseFont* japaneseFont = nullptr);
  void handleClient();
  bool consumeJapaneseFontReloadPending();
  bool started() const { return started_; }
 private:
  void registerRoutes();
  void startIfReady();
  void sendIndex();
  void sendDownload(const DownloadFile& download);
  void sendEarthquakeHistoryDownload();
  void sendText(int status, const char* message);
  bool sendSpeechBusy();
  WebServer server_{80};
  bool storageAvailable_ = false;
  bool routesRegistered_ = false;
  bool started_ = false;
  EarthquakeHistoryService* earthquakeHistory_ = nullptr;
  EarthquakeService* earthquakeService_ = nullptr;
  SpeechService* speechService_ = nullptr;
  JapaneseFont* japaneseFont_ = nullptr;
  bool japaneseFontReloadPending_ = false;
};
