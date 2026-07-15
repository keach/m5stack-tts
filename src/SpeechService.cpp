#include "SpeechService.h"

#include <SD.h>
#include <aquestalk.h>

#include "driver/i2s.h"
#include "secrets.h"

namespace {
constexpr char DICTIONARY_PATH[] = "/aq_dic/aqdic_m.bin";
constexpr size_t DICTIONARY_VIRTUAL_ADDRESS = 0x10001000U;
constexpr int FRAME_LENGTH = 32;
constexpr int PHONETIC_BUFFER_SIZE = 1024;
constexpr int AUDIO_SAMPLE_RATE = 24000;
constexpr i2s_port_t I2S_PORT = I2S_NUM_0;

File dictionaryFile;
}  // namespace

extern "C" size_t aqdic_open() {
  dictionaryFile = SD.open(DICTIONARY_PATH, FILE_READ);
  return dictionaryFile ? DICTIONARY_VIRTUAL_ADDRESS : 0;
}

extern "C" void aqdic_close() {
  if (dictionaryFile) {
    dictionaryFile.close();
  }
}

extern "C" size_t aqdic_read(size_t position, size_t size, void* buffer) {
  if (!dictionaryFile || position < DICTIONARY_VIRTUAL_ADDRESS) {
    return 0;
  }
  if (!dictionaryFile.seek(position - DICTIONARY_VIRTUAL_ADDRESS)) {
    return 0;
  }
  return dictionaryFile.read(static_cast<uint8_t*>(buffer), size);
}

bool SpeechService::initializeAudio() {
  const i2s_config_t config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX |
                                     I2S_MODE_DAC_BUILT_IN),
      .sample_rate = AUDIO_SAMPLE_RATE,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_MSB,
      .intr_alloc_flags = 0,
      .dma_buf_count = 4,
      .dma_buf_len = 384,
      .use_apll = false,
  };

  if (i2s_driver_install(I2S_PORT, &config, 0, nullptr) != ESP_OK) {
    return false;
  }
  if (i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN) != ESP_OK) {
    i2s_driver_uninstall(I2S_PORT);
    return false;
  }
  AqResample_Reset();
  return true;
}

bool SpeechService::begin() {
  if (initialized_) {
    return true;
  }
  if (!SD.exists(DICTIONARY_PATH)) {
    Serial.printf("AquesTalk dictionary not found: %s\n", DICTIONARY_PATH);
    return false;
  }

  languageWorkBuffer_ =
      static_cast<uint8_t*>(malloc(SIZE_AQK2R_MIN_WORK_BUF));
  speechWorkBuffer_ = static_cast<uint32_t*>(
      malloc(AQ_SIZE_WORKBUF * sizeof(uint32_t)));
  if (!languageWorkBuffer_ || !speechWorkBuffer_) {
    Serial.println("AquesTalk work buffer allocation failed.");
    free(languageWorkBuffer_);
    free(speechWorkBuffer_);
    languageWorkBuffer_ = nullptr;
    speechWorkBuffer_ = nullptr;
    return false;
  }

  int result = CAqK2R_Create(languageWorkBuffer_, SIZE_AQK2R_MIN_WORK_BUF);
  if (result != 0) {
    Serial.printf("CAqK2R_Create failed: %d\n", result);
    free(languageWorkBuffer_);
    free(speechWorkBuffer_);
    languageWorkBuffer_ = nullptr;
    speechWorkBuffer_ = nullptr;
    return false;
  }

  const char* licenseKey =
      AQUESTALK_LICENSE_KEY[0] == '\0' ? nullptr : AQUESTALK_LICENSE_KEY;
  result = CAqTkPicoF_Init(speechWorkBuffer_, FRAME_LENGTH, licenseKey);
  if (result != 0 || !initializeAudio()) {
    Serial.printf("AquesTalk audio initialization failed: %d\n", result);
    CAqK2R_Release();
    free(languageWorkBuffer_);
    free(speechWorkBuffer_);
    languageWorkBuffer_ = nullptr;
    speechWorkBuffer_ = nullptr;
    return false;
  }

  if (xTaskCreatePinnedToCore(taskEntry, "aquestalk", 4096, this, 2,
                              &taskHandle_, APP_CPU_NUM) != pdPASS) {
    Serial.println("AquesTalk task creation failed.");
    i2s_driver_uninstall(I2S_PORT);
    CAqK2R_Release();
    free(languageWorkBuffer_);
    free(speechWorkBuffer_);
    languageWorkBuffer_ = nullptr;
    speechWorkBuffer_ = nullptr;
    return false;
  }

  initialized_ = true;
  Serial.println("AquesTalk speech service ready.");
  return true;
}

bool SpeechService::speak(const char* utf8Text, int speed) {
  if (!initialized_ || !utf8Text || utf8Text[0] == '\0') {
    return false;
  }

  stop();
  char phonetic[PHONETIC_BUFFER_SIZE];
  int result = CAqK2R_Convert(utf8Text, phonetic, sizeof(phonetic));
  if (result != 0) {
    Serial.printf("CAqK2R_Convert failed: %d\n", result);
    return false;
  }

  speed = constrain(speed, 50, 300);
  result = CAqTkPicoF_SetKoe(reinterpret_cast<const uint8_t*>(phonetic),
                             speed, 0xffffU);
  if (result != 0) {
    Serial.printf("CAqTkPicoF_SetKoe failed: %d\n", result);
    return false;
  }

  stopRequested_ = false;
  speaking_ = true;
  xTaskNotifyGive(taskHandle_);
  return true;
}

bool SpeechService::playAlertTone() {
  if (!initialized_) {
    return false;
  }

  stop();
  constexpr int FREQUENCY_HZ = 880;
  constexpr int BEEP_DURATION_MS = 180;
  constexpr int GAP_DURATION_MS = 120;
  constexpr int BEEP_COUNT = 3;
  constexpr int16_t AMPLITUDE = 12000;
  constexpr size_t CHUNK_SAMPLES = 128;
  uint16_t output[CHUNK_SAMPLES * 2];
  uint32_t phase = 0;
  bool high = true;

  auto writeLevel = [&](int sampleCount, int16_t amplitude) {
    while (sampleCount > 0) {
      const size_t count = min(static_cast<size_t>(sampleCount), CHUNK_SAMPLES);
      for (size_t index = 0; index < count; ++index) {
        int16_t signedSample = amplitude;
        if (amplitude != 0) {
          signedSample = high ? amplitude : -amplitude;
          phase += FREQUENCY_HZ * 2;
          if (phase >= AUDIO_SAMPLE_RATE) {
            phase -= AUDIO_SAMPLE_RATE;
            high = !high;
          }
        }
        const uint16_t sample = static_cast<uint16_t>(signedSample) ^ 0x8000U;
        output[index * 2] = sample;
        output[index * 2 + 1] = sample;
      }
      size_t bytesWritten = 0;
      i2s_write(I2S_PORT, output, count * 2 * sizeof(uint16_t), &bytesWritten,
                portMAX_DELAY);
      sampleCount -= count;
    }
  };

  for (int beep = 0; beep < BEEP_COUNT; ++beep) {
    writeLevel(AUDIO_SAMPLE_RATE * BEEP_DURATION_MS / 1000, AMPLITUDE);
    if (beep + 1 < BEEP_COUNT) {
      writeLevel(AUDIO_SAMPLE_RATE * GAP_DURATION_MS / 1000, 0);
    }
  }
  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

void SpeechService::stop() {
  if (!speaking_) {
    return;
  }
  stopRequested_ = true;
  while (speaking_) {
    vTaskDelay(1);
  }
}

bool SpeechService::isSpeaking() const { return speaking_; }

void SpeechService::taskEntry(void* argument) {
  static_cast<SpeechService*>(argument)->runTask();
}

void SpeechService::runTask() {
  int16_t pcm[FRAME_LENGTH];
  uint16_t dacSamples[FRAME_LENGTH * 3 * 2];

  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (!stopRequested_) {
      uint16_t length = 0;
      if (CAqTkPicoF_SyntheFrame(pcm, &length) != 0) {
        break;
      }

      size_t outputIndex = 0;
      for (uint16_t index = 0; index < length; ++index) {
        int16_t upsampled[3];
        AqResample_Conv(pcm[index], upsampled);
        for (int sampleIndex = 0; sampleIndex < 3; ++sampleIndex) {
          const uint16_t sample =
              static_cast<uint16_t>(upsampled[sampleIndex]) ^ 0x8000U;
          dacSamples[outputIndex++] = sample;
          dacSamples[outputIndex++] = sample;
        }
      }

      size_t bytesWritten = 0;
      i2s_write(I2S_PORT, dacSamples, outputIndex * sizeof(uint16_t),
                &bytesWritten, portMAX_DELAY);
    }
    i2s_zero_dma_buffer(I2S_PORT);
    speaking_ = false;
  }
}
