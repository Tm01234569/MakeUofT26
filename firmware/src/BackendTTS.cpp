#include "BackendTTS.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include "Audio.h"

BackendTTS::BackendTTS() {}

void BackendTTS::setConfig(const char* baseUrl,
                           const char* apiKey,
                           const char* voiceId,
                           const char* modelId,
                           const char* outputFormat) {
  _baseUrl = baseUrl == nullptr ? "" : baseUrl;
  _apiKey = apiKey == nullptr ? "" : apiKey;
  _voiceId = voiceId == nullptr ? "EST9Ui6982FZPSi7gCHi" : voiceId;
  _modelId = modelId == nullptr ? "eleven_flash_v2_5" : modelId;
  _outputFormat = outputFormat == nullptr ? "mp3_22050_32" : outputFormat;
}

bool BackendTTS::isConfigured() const {
  return _baseUrl.length() > 0 && _apiKey.length() > 0;
}

bool BackendTTS::speak(const String& text) {
  if (!isConfigured() || text.length() == 0) return false;

  if (!SPIFFS.begin(true)) {
    Serial.println("[BackendTTS] SPIFFS init failed");
    return false;
  }

  DynamicJsonDocument doc(2048);
  doc["text"] = text;
  doc["voice_id"] = _voiceId;
  doc["model_id"] = _modelId;
  doc["output_format"] = _outputFormat;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  String url = _baseUrl + "/v1/tts/synthesize";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", _apiKey);
  int code = http.POST(payload);
  if (code < 200 || code >= 300) {
    String err = http.getString();
    Serial.printf("[BackendTTS] HTTP %d: %s\n", code, err.c_str());
    http.end();
    return false;
  }

  File f = SPIFFS.open("/backend_tts.mp3", FILE_WRITE);
  if (!f) {
    Serial.println("[BackendTTS] Failed to open cache file");
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  size_t total = 0;
  unsigned long lastDataMs = millis();
  while (http.connected() || stream->available()) {
    int available = stream->available();
    if (available > 0) {
      int n = stream->readBytes(buf, (available > (int)sizeof(buf)) ? sizeof(buf) : available);
      if (n > 0) {
        f.write(buf, n);
        total += (size_t)n;
        lastDataMs = millis();
      }
    } else {
      if (millis() - lastDataMs > 5000) break;
      delay(2);
    }
  }

  f.close();
  http.end();

  if (total == 0) {
    Serial.println("[BackendTTS] Empty audio response");
    return false;
  }

  extern Audio audio;
  bool ok = audio.connecttoFS(SPIFFS, "/backend_tts.mp3");
  if (!ok) {
    Serial.println("[BackendTTS] audio.connecttoFS failed");
  }
  return ok;
}
