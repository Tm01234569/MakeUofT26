#include "ElevenLabsTTS.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <WiFiClientSecure.h>
#include "Audio.h"

ElevenLabsTTS::ElevenLabsTTS() {}

void ElevenLabsTTS::setConfig(const char* apiKey,
                              const char* voiceId,
                              const char* modelId,
                              const char* outputFormat) {
  _apiKey = apiKey == nullptr ? "" : apiKey;
  _voiceId = voiceId == nullptr ? "" : voiceId;
  _modelId = modelId == nullptr ? "eleven_flash_v2_5" : modelId;
  _outputFormat = outputFormat == nullptr ? "mp3_22050_32" : outputFormat;
}

bool ElevenLabsTTS::isConfigured() const {
  return _apiKey.length() > 0 && _voiceId.length() > 0;
}

bool ElevenLabsTTS::speak(const String& text) {
  if (!isConfigured() || text.length() == 0) {
    return false;
  }

  if (!SPIFFS.begin(true)) {
    Serial.println("[ElevenLabsTTS] SPIFFS init failed");
    return false;
  }

  DynamicJsonDocument doc(1024);
  doc["text"] = text;
  doc["model_id"] = _modelId;

  String payload;
  serializeJson(doc, payload);

  String url = "https://api.elevenlabs.io/v1/text-to-speech/" + _voiceId;
  if (_outputFormat.length() > 0) {
    url += "?output_format=" + _outputFormat;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "audio/mpeg");
  http.addHeader("xi-api-key", _apiKey);

  int code = http.POST(payload);
  if (code < 200 || code >= 300) {
    String err = http.getString();
    Serial.printf("[ElevenLabsTTS] HTTP %d: %s\n", code, err.c_str());
    http.end();
    return false;
  }

  File f = SPIFFS.open("/elevenlabs_tts.mp3", FILE_WRITE);
  if (!f) {
    Serial.println("[ElevenLabsTTS] Failed to open cache file");
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
      if (millis() - lastDataMs > 5000) {
        break;
      }
      delay(2);
    }
  }

  f.close();
  http.end();

  if (total == 0) {
    Serial.println("[ElevenLabsTTS] Empty audio response");
    return false;
  }

  extern Audio audio;
  bool ok = audio.connecttoFS(SPIFFS, "/elevenlabs_tts.mp3");
  if (!ok) {
    Serial.println("[ElevenLabsTTS] audio.connecttoFS failed");
  }
  return ok;
}
