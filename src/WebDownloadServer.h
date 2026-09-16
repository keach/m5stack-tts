#pragma once
#include <WebServer.h>

class EarthquakeHistoryService;
class EarthquakeService;

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
             EarthquakeService* earthquakeService = nullptr);
  void handleClient();
  bool started() const { return started_; }
 private:
  void registerRoutes();
  void startIfReady();
  void sendIndex();
  void sendDownload(const DownloadFile& download);
  void sendEarthquakeHistoryDownload();
  void sendText(int status, const char* message);
  WebServer server_{80};
  bool storageAvailable_ = false;
  bool routesRegistered_ = false;
  bool started_ = false;
  EarthquakeHistoryService* earthquakeHistory_ = nullptr;
  EarthquakeService* earthquakeService_ = nullptr;
};
