#include "EarthquakeSpeechService.h"

#include "SpeechService.h"

namespace {
constexpr char NVS_NAMESPACE[] = "eq_speech";
constexpr char EEW_EVENT_KEY[] = "eew_event";
constexpr char EEW_SIGNATURE_KEY[] = "eew_sig";
constexpr char EARTHQUAKE_KEY[] = "quake_key";
constexpr char EARTHQUAKE_SIGNATURE_KEY[] = "quake_sig";

constexpr uint8_t PRIORITY_EEW_CANCEL = 0;
constexpr uint8_t PRIORITY_EEW = 1;
constexpr uint8_t PRIORITY_EARTHQUAKE = 2;

void copyText(char* destination, size_t capacity, const char* source) {
  if (!destination || capacity == 0) return;
  strlcpy(destination, source ? source : "", capacity);
}
}  // namespace

void EarthquakeSpeechService::begin(bool eewEnabled, bool earthquakeEnabled) {
  preferences_.begin(NVS_NAMESPACE, false);
  setEnabled(eewEnabled, earthquakeEnabled);
  const String eewEvent = preferences_.getString(EEW_EVENT_KEY, "");
  const String eewSignature = preferences_.getString(EEW_SIGNATURE_KEY, "");
  const String earthquakeKey = preferences_.getString(EARTHQUAKE_KEY, "");
  const String earthquakeSignature =
      preferences_.getString(EARTHQUAKE_SIGNATURE_KEY, "");
  if (eewEvent.length() > 0) {
    copyText(eewStates_[0].eventId, sizeof(eewStates_[0].eventId),
             eewEvent.c_str());
    copyText(eewStates_[0].signature, sizeof(eewStates_[0].signature),
             eewSignature.c_str());
    eewStateCount_ = 1;
  }
  copyText(lastEarthquakeKey_, sizeof(lastEarthquakeKey_),
           earthquakeKey.c_str());
  copyText(lastEarthquakeSignature_, sizeof(lastEarthquakeSignature_),
           earthquakeSignature.c_str());
}

void EarthquakeSpeechService::setEnabled(bool eewEnabled,
                                         bool earthquakeEnabled) {
  eewEnabled_ = eewEnabled;
  earthquakeEnabled_ = earthquakeEnabled;
  for (size_t index = 0; index < queueCount_;) {
    if (!isEnabled(queue_[index].kind)) {
      removeAt(index);
    } else {
      ++index;
    }
  }
}

void EarthquakeSpeechService::enqueue(const SeismicEvent& event) {
  if (event.type == SeismicEventType::Eew && event.cancelled &&
      !hasEewStarted(event.eventId)) {
    for (size_t index = 0; index < queueCount_;) {
      if (queue_[index].kind == RequestKind::Eew &&
          strcmp(queue_[index].event.eventId, event.eventId) == 0) {
        removeAt(index);
      } else {
        ++index;
      }
    }
    return;
  }
  Request request;
  if (!buildRequest(event, &request)) return;
  enqueueRequest(request);
}

void EarthquakeSpeechService::loop(SpeechService& speech) {
  if (active_ && !speech.isSpeaking()) {
    active_ = false;
  }
  if (queueCount_ == 0) return;

  const size_t index = highestPriorityIndex();
  const Request& request = queue_[index];
  if (speech.isSpeaking()) {
    if (request.priority > PRIORITY_EEW) return;
    speech.stop();
    active_ = false;
  }

  if (!speech.speak(request.message)) {
    Serial.printf("Earthquake speech dropped because playback could not start: %s\n",
                  request.message);
    removeAt(index);
    return;
  }
  rememberStarted(request);
  active_ = true;
  removeAt(index);
}

bool EarthquakeSpeechService::activeOrPending() const {
  return active_ || queueCount_ > 0;
}

bool EarthquakeSpeechService::isEnabled(RequestKind kind) const {
  return kind == RequestKind::Eew ? eewEnabled_ : earthquakeEnabled_;
}

bool EarthquakeSpeechService::buildRequest(const SeismicEvent& event,
                                           Request* request) const {
  if (!request || event.type == SeismicEventType::None) return false;
  if (event.type == SeismicEventType::Eew) {
    return buildEewRequest(event, request);
  }
  if (event.type == SeismicEventType::Earthquake) {
    return buildEarthquakeRequest(event, request);
  }
  return false;
}

bool EarthquakeSpeechService::buildEewRequest(const SeismicEvent& event,
                                              Request* request) const {
  if (!eewEnabled_ || event.eventId[0] == '\0') return false;

  request->kind = RequestKind::Eew;
  request->priority = event.cancelled ? PRIORITY_EEW_CANCEL : PRIORITY_EEW;
  const size_t stateIndex = findEewState(event.eventId);
  const bool previouslyStarted = stateIndex < eewStateCount_;
  request->eewInitialPending = !previouslyStarted;
  request->event = event;
  snprintf(request->signature, sizeof(request->signature),
           "%d|%d|%d|%.1f|%s|%s", event.cancelled, event.targetMatched,
           event.maxScale, event.magnitude, event.targetAreas,
           event.hypocenter);
  if (previouslyStarted &&
      strcmp(eewStates_[stateIndex].signature, request->signature) == 0) {
    return false;
  }

  appendTestPrefix(event, request->message, sizeof(request->message));
  if (event.cancelled) {
    strlcat(request->message, "先ほどの緊急地震速報は取り消されました。",
            sizeof(request->message));
    return true;
  }

  const bool followUp = previouslyStarted;
  strlcat(request->message,
          followUp ? "緊急地震速報の続報です。" : "緊急地震速報です。",
          sizeof(request->message));
  if (event.targetMatched) {
    char target[240];
    snprintf(target, sizeof(target), "%sで強い揺れに警戒してください。",
             event.targetAreas);
    strlcat(request->message, target, sizeof(request->message));
  } else {
    strlcat(request->message, "設定地域は警報の対象外です。",
            sizeof(request->message));
  }
  char magnitude[32];
  if (event.magnitude < 0.0F) {
    copyText(magnitude, sizeof(magnitude), "不明");
  } else {
    snprintf(magnitude, sizeof(magnitude), "%.1f", event.magnitude);
  }
  char details[256];
  snprintf(details, sizeof(details),
           "震源は%s、予想最大震度は%s、マグニチュードは%sです。",
           event.hypocenter, scaleText(event.maxScale), magnitude);
  strlcat(request->message, details, sizeof(request->message));
  return true;
}

bool EarthquakeSpeechService::buildEarthquakeRequest(
    const SeismicEvent& event, Request* request) const {
  if (!earthquakeEnabled_ || !event.targetMatched ||
      event.logicalKey[0] == '\0') {
    return false;
  }

  request->kind = RequestKind::Earthquake;
  request->priority = PRIORITY_EARTHQUAKE;
  request->event = event;
  snprintf(request->signature, sizeof(request->signature),
           "%d|%d|%d|%.1f|%s|%s", event.corrected, event.maxScale,
           event.nationalMaxScale, event.magnitude, event.targetAreas,
           event.hypocenter);
  if (strcmp(lastEarthquakeKey_, event.logicalKey) == 0 &&
      strcmp(lastEarthquakeSignature_, request->signature) == 0) {
    return false;
  }

  appendTestPrefix(event, request->message, sizeof(request->message));
  char magnitude[32];
  if (event.magnitude < 0.0F) {
    copyText(magnitude, sizeof(magnitude), "不明");
  } else {
    snprintf(magnitude, sizeof(magnitude), "%.1f", event.magnitude);
  }
  char message[440];
  snprintf(message, sizeof(message),
           "地震情報です。%sごろ、%sを震源とするマグニチュード%sの地震がありました。"
           "設定地域の最大震度は%s",
           event.eventTime, event.hypocenter, magnitude,
           scaleText(event.maxScale));
  strlcat(request->message, message, sizeof(request->message));
  if (event.maxScale != event.nationalMaxScale) {
    char national[96];
    snprintf(national, sizeof(national), "、全国の最大震度は%sです。",
             scaleText(event.nationalMaxScale));
    strlcat(request->message, national, sizeof(request->message));
  } else {
    strlcat(request->message, "です。", sizeof(request->message));
  }
  return true;
}

void EarthquakeSpeechService::enqueueRequest(const Request& request) {
  for (size_t index = 0; index < queueCount_; ++index) {
    Request& queued = queue_[index];
    const bool sameEew = request.kind == RequestKind::Eew &&
                         queued.kind == RequestKind::Eew &&
                         strcmp(request.event.eventId,
                                queued.event.eventId) == 0;
    const bool sameEarthquake =
        request.kind == RequestKind::Earthquake &&
        queued.kind == RequestKind::Earthquake &&
        strcmp(request.event.logicalKey, queued.event.logicalKey) == 0;
    if (sameEew || sameEarthquake) {
      const bool retainEewInitialPending = queued.eewInitialPending;
      queued = request;
      if (sameEew) {
        queued.eewInitialPending = retainEewInitialPending;
      }
      return;
    }
  }

  if (queueCount_ == MAX_QUEUE_SIZE) {
    size_t removeIndex = MAX_QUEUE_SIZE;
    for (size_t index = 0; index < queueCount_; ++index) {
      if (queue_[index].kind == RequestKind::Earthquake) {
        removeIndex = index;
        break;
      }
    }
    if (removeIndex == MAX_QUEUE_SIZE) {
      for (size_t index = 0; index < queueCount_; ++index) {
        if (queue_[index].kind == RequestKind::Eew &&
            !queue_[index].event.cancelled &&
            !queue_[index].eewInitialPending) {
          removeIndex = index;
          break;
        }
      }
    }
    if (removeIndex == MAX_QUEUE_SIZE) {
      Serial.println("Earthquake speech queue full; request dropped.");
      return;
    }
    Serial.println("Earthquake speech queue full; older request dropped.");
    removeAt(removeIndex);
  }
  queue_[queueCount_++] = request;
}

void EarthquakeSpeechService::removeAt(size_t index) {
  if (index >= queueCount_) return;
  for (size_t next = index + 1; next < queueCount_; ++next) {
    queue_[next - 1] = queue_[next];
  }
  --queueCount_;
}

size_t EarthquakeSpeechService::highestPriorityIndex() const {
  size_t selected = 0;
  for (size_t index = 1; index < queueCount_; ++index) {
    if (queue_[index].priority < queue_[selected].priority) {
      selected = index;
    }
  }
  return selected;
}

void EarthquakeSpeechService::rememberStarted(const Request& request) {
  if (request.kind == RequestKind::Eew) {
    rememberEewStarted(request);
  } else {
    copyText(lastEarthquakeKey_, sizeof(lastEarthquakeKey_),
             request.event.logicalKey);
    copyText(lastEarthquakeSignature_, sizeof(lastEarthquakeSignature_),
             request.signature);
    preferences_.putString(EARTHQUAKE_KEY, lastEarthquakeKey_);
    preferences_.putString(EARTHQUAKE_SIGNATURE_KEY,
                           lastEarthquakeSignature_);
  }
  Serial.printf("Earthquake speech started: %s\n", request.message);
}

bool EarthquakeSpeechService::hasEewStarted(const char* eventId) const {
  return findEewState(eventId) < eewStateCount_;
}

size_t EarthquakeSpeechService::findEewState(const char* eventId) const {
  for (size_t index = 0; index < eewStateCount_; ++index) {
    if (strcmp(eewStates_[index].eventId, eventId) == 0) return index;
  }
  return eewStateCount_;
}

void EarthquakeSpeechService::rememberEewStarted(const Request& request) {
  size_t index = findEewState(request.event.eventId);
  if (index < eewStateCount_) {
    for (size_t next = index + 1; next < eewStateCount_; ++next) {
      eewStates_[next - 1] = eewStates_[next];
    }
    --eewStateCount_;
  }
  if (eewStateCount_ == MAX_EEW_STATE_COUNT) {
    for (size_t next = 1; next < eewStateCount_; ++next) {
      eewStates_[next - 1] = eewStates_[next];
    }
    --eewStateCount_;
  }
  EewState& state = eewStates_[eewStateCount_++];
  copyText(state.eventId, sizeof(state.eventId), request.event.eventId);
  copyText(state.signature, sizeof(state.signature), request.signature);
  preferences_.putString(EEW_EVENT_KEY, state.eventId);
  preferences_.putString(EEW_SIGNATURE_KEY, state.signature);
}

const char* EarthquakeSpeechService::scaleText(int scale) {
  switch (scale) {
    case 10: return "1";
    case 20: return "2";
    case 30: return "3";
    case 40: return "4";
    case 45: return "5弱";
    case 46:
    case 99: return "5弱以上";
    case 50: return "5強";
    case 55: return "6弱";
    case 60: return "6強";
    case 70: return "7";
    default: return "不明";
  }
}

void EarthquakeSpeechService::appendTestPrefix(const SeismicEvent& event,
                                                char* message,
                                                size_t capacity) {
  if (event.test) strlcat(message, "訓練です。", capacity);
}
