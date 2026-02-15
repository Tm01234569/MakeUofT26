#include "GeminiASRChat.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

GeminiASRChat::GeminiASRChat(const char* apiKey, const char* model, const char* baseUrl)
    : _apiKey(apiKey == nullptr ? "" : apiKey),
      _model(model == nullptr ? "gemini-2.0-flash" : model),
      _baseUrl(baseUrl == nullptr ? "https://generativelanguage.googleapis.com" : baseUrl) {}

void GeminiASRChat::setApiConfig(const char* apiKey, const char* model, const char* baseUrl) {
  if (apiKey != nullptr) _apiKey = apiKey;
  if (model != nullptr) _model = model;
  if (baseUrl != nullptr) _baseUrl = baseUrl;
}

void GeminiASRChat::setModel(const char* model) {
  if (model != nullptr && strlen(model) > 0) {
    _model = model;
  }
}

bool GeminiASRChat::initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin) {
  _I2S.setPins(i2sSckPin, i2sWsPin, -1, i2sSdPin);
  if (!_I2S.begin(I2S_MODE_STD, _sampleRate, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("INMP441 I2S initialization failed!");
    return false;
  }
  _micInitialized = true;
  Serial.println("INMP441 microphone initialized");

  delay(300);
  for (int i = 0; i < 1200; ++i) {
    _I2S.read();
  }
  return true;
}

void GeminiASRChat::setAudioParams(int sampleRate, int bitsPerSample, int channels) {
  _sampleRate = sampleRate;
  _bitsPerSample = bitsPerSample;
  _channels = channels;
}

void GeminiASRChat::setSilenceDuration(unsigned long duration) {
  _silenceDuration = duration;
}

void GeminiASRChat::setMaxRecordingSeconds(int seconds) {
  if (seconds < 1) {
    seconds = 1;
  }
  if (seconds > 8) {
    seconds = 8;
  }
  _maxSeconds = seconds;
}

bool GeminiASRChat::connectWebSocket() {
  return true;
}

bool GeminiASRChat::ensurePcmBuffer() {
  size_t targetSeconds = (size_t)_maxSeconds;
  if (!psramFound() && targetSeconds > 2) {
    targetSeconds = 2;
  }

  size_t targetSamples = (size_t)_sampleRate * targetSeconds;
  if (targetSamples < (size_t)_sampleRate) {
    targetSamples = (size_t)_sampleRate;
  }

  if (_pcmBuffer != nullptr && _pcmCapacitySamples >= targetSamples) {
    return true;
  }

  if (_pcmBuffer != nullptr) {
    free(_pcmBuffer);
    _pcmBuffer = nullptr;
    _pcmCapacitySamples = 0;
  }

  size_t bytes = targetSamples * sizeof(int16_t);
  if (psramFound()) {
    _pcmBuffer = (int16_t*)ps_malloc(bytes);
  } else {
    _pcmBuffer = (int16_t*)malloc(bytes);
  }

  if (_pcmBuffer == nullptr) {
    Serial.println("[Gemini ASR] Failed to allocate audio buffer");
    return false;
  }

  _pcmCapacitySamples = targetSamples;
  return true;
}

bool GeminiASRChat::startRecording() {
  if (!_micInitialized) {
    Serial.println("[Gemini ASR] Microphone not initialized");
    return false;
  }
  if (_apiKey.length() == 0) {
    Serial.println("[Gemini ASR] Missing gemini_apiKey");
    return false;
  }
  if (!ensurePcmBuffer()) {
    return false;
  }

  _pcmSamples = 0;
  _hasSpeech = false;
  _hasNewResult = false;
  _recognizedText = "";
  _recordingStartMs = millis();
  _lastSpeechMs = _recordingStartMs;
  _lastDotMs = _recordingStartMs;
  _isRecording = true;

  Serial.println("========================================");
  Serial.println("Recording started...");
  Serial.println("========================================");
  return true;
}

void GeminiASRChat::stopRecording() {
  _isRecording = false;
}

bool GeminiASRChat::isRecording() {
  return _isRecording;
}

void GeminiASRChat::loop() {
  if (!_isRecording) {
    return;
  }

  unsigned long now = millis();
  if (now - _lastDotMs > 1000) {
    Serial.print(".");
    _lastDotMs = now;
  }

  for (int i = 0; i < _samplesPerRead; ++i) {
    if (!_I2S.available()) {
      break;
    }

    int sample = _I2S.read();
    int16_t s = (int16_t)sample;
    if (_pcmSamples < _pcmCapacitySamples) {
      _pcmBuffer[_pcmSamples++] = s;
    }

    if (abs((int)s) >= _speechThreshold) {
      _hasSpeech = true;
      _lastSpeechMs = now;
    }
  }

  bool timedOut = (now - _recordingStartMs) >= (unsigned long)(_maxSeconds * 1000);
  bool silenceDone = _hasSpeech && ((now - _lastSpeechMs) >= _silenceDuration) && (_pcmSamples > (size_t)(_sampleRate / 4));
  bool noSpeechTimeout = !_hasSpeech && ((now - _recordingStartMs) >= 4500UL);
  bool bufferFull = _pcmSamples >= _pcmCapacitySamples;

  if (!timedOut && !silenceDone && !noSpeechTimeout && !bufferFull) {
    return;
  }

  _isRecording = false;
  Serial.println();

  if (!_hasSpeech || _pcmSamples < (size_t)(_sampleRate / 6)) {
    Serial.println("[Gemini ASR] No speech detected");
    if (_timeoutNoSpeechCallback != nullptr) {
      _timeoutNoSpeechCallback();
    }
    return;
  }

  String result = transcribeCurrentBuffer();
  if (result.length() == 0) {
    Serial.println("[Gemini ASR] Empty transcription result");
    return;
  }

  _recognizedText = result;
  _hasNewResult = true;
  if (_resultCallback != nullptr) {
    _resultCallback(result);
  }
}

String GeminiASRChat::transcribeCurrentBuffer() {
  String payload = buildPayloadFromPcm16(_pcmBuffer, _pcmSamples);
  if (payload.length() == 0) {
    return "";
  }
  String response = postTranscription(payload);
  if (response.length() == 0) {
    return "";
  }
  return extractTextFromResponse(response);
}

String GeminiASRChat::postTranscription(const String& payload) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String endpoint = _baseUrl + "/v1beta/models/" + _model + ":generateContent?key=" + _apiKey;
  if (!http.begin(client, endpoint)) {
    Serial.println("[Gemini ASR] Failed to open HTTP session");
    return "";
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  if (code != 200) {
    String err = http.getString();
    Serial.printf("[Gemini ASR] HTTP %d: %s\n", code, err.c_str());
    http.end();
    return "";
  }

  String response = http.getString();
  http.end();
  return response;
}

String GeminiASRChat::extractTextFromResponse(const String& response) const {
  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    return "";
  }

  JsonArray candidates = doc["candidates"].as<JsonArray>();
  if (candidates.isNull() || candidates.size() == 0) {
    return "";
  }

  JsonArray parts = candidates[0]["content"]["parts"].as<JsonArray>();
  if (parts.isNull() || parts.size() == 0) {
    return "";
  }

  String text = parts[0]["text"].as<String>();
  text.trim();
  return text;
}

String GeminiASRChat::buildPayloadFromPcm16(const int16_t* pcm, size_t samples) const {
  String audioB64 = buildWavBase64FromPcm16(pcm, samples);
  if (audioB64.length() == 0) {
    return "";
  }

  DynamicJsonDocument doc(2048);
  JsonArray contents = doc.createNestedArray("contents");
  JsonObject content = contents.createNestedObject();
  JsonArray parts = content.createNestedArray("parts");

  JsonObject textPart = parts.createNestedObject();
  textPart["text"] = "Transcribe this spoken audio. Return only plain text without labels.";

  JsonObject audioPart = parts.createNestedObject();
  JsonObject inlineData = audioPart.createNestedObject("inline_data");
  inlineData["mime_type"] = "audio/wav";
  inlineData["data"] = audioB64;

  JsonObject generationConfig = doc.createNestedObject("generationConfig");
  generationConfig["temperature"] = 0.0;

  String payload;
  serializeJson(doc, payload);
  return payload;
}

String GeminiASRChat::buildWavBase64FromPcm16(const int16_t* pcm, size_t samples) const {
  if (pcm == nullptr || samples == 0 || _channels != 1 || _bitsPerSample != 16) {
    return "";
  }

  const uint32_t dataBytes = (uint32_t)(samples * sizeof(int16_t));
  const uint32_t fileSizeMinus8 = 36 + dataBytes;
  const uint32_t byteRate = (uint32_t)_sampleRate * (uint32_t)_channels * (uint32_t)(_bitsPerSample / 8);
  const uint16_t blockAlign = (uint16_t)(_channels * (_bitsPerSample / 8));

  uint8_t header[44];
  memset(header, 0, sizeof(header));
  memcpy(header + 0, "RIFF", 4);
  header[4] = (uint8_t)(fileSizeMinus8 & 0xFF);
  header[5] = (uint8_t)((fileSizeMinus8 >> 8) & 0xFF);
  header[6] = (uint8_t)((fileSizeMinus8 >> 16) & 0xFF);
  header[7] = (uint8_t)((fileSizeMinus8 >> 24) & 0xFF);
  memcpy(header + 8, "WAVE", 4);
  memcpy(header + 12, "fmt ", 4);
  header[16] = 16;
  header[20] = 1;
  header[22] = (uint8_t)_channels;
  header[24] = (uint8_t)(_sampleRate & 0xFF);
  header[25] = (uint8_t)((_sampleRate >> 8) & 0xFF);
  header[26] = (uint8_t)((_sampleRate >> 16) & 0xFF);
  header[27] = (uint8_t)((_sampleRate >> 24) & 0xFF);
  header[28] = (uint8_t)(byteRate & 0xFF);
  header[29] = (uint8_t)((byteRate >> 8) & 0xFF);
  header[30] = (uint8_t)((byteRate >> 16) & 0xFF);
  header[31] = (uint8_t)((byteRate >> 24) & 0xFF);
  header[32] = (uint8_t)(blockAlign & 0xFF);
  header[33] = (uint8_t)((blockAlign >> 8) & 0xFF);
  header[34] = (uint8_t)(_bitsPerSample & 0xFF);
  header[35] = (uint8_t)((_bitsPerSample >> 8) & 0xFF);
  memcpy(header + 36, "data", 4);
  header[40] = (uint8_t)(dataBytes & 0xFF);
  header[41] = (uint8_t)((dataBytes >> 8) & 0xFF);
  header[42] = (uint8_t)((dataBytes >> 16) & 0xFF);
  header[43] = (uint8_t)((dataBytes >> 24) & 0xFF);

  const size_t totalBytes = sizeof(header) + dataBytes;
  String out;
  out.reserve(((totalBytes + 2) / 3) * 4);

  uint8_t remainder[3] = {0, 0, 0};
  uint8_t remainderLen = 0;
  appendBase64Bytes(out, header, sizeof(header), remainder, remainderLen);
  appendBase64Bytes(out, (const uint8_t*)pcm, dataBytes, remainder, remainderLen);

  static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (remainderLen == 1) {
    uint8_t b0 = remainder[0];
    out += kB64[(b0 >> 2) & 0x3F];
    out += kB64[(b0 & 0x03) << 4];
    out += "==";
  } else if (remainderLen == 2) {
    uint8_t b0 = remainder[0];
    uint8_t b1 = remainder[1];
    out += kB64[(b0 >> 2) & 0x3F];
    out += kB64[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
    out += kB64[(b1 & 0x0F) << 2];
    out += '=';
  }

  return out;
}

void GeminiASRChat::appendBase64Bytes(String& out,
                                      const uint8_t* data,
                                      size_t len,
                                      uint8_t remainder[3],
                                      uint8_t& remainderLen) {
  static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t idx = 0;

  if (remainderLen > 0) {
    while (remainderLen < 3 && idx < len) {
      remainder[remainderLen++] = data[idx++];
    }
    if (remainderLen == 3) {
      uint32_t v = ((uint32_t)remainder[0] << 16) | ((uint32_t)remainder[1] << 8) | remainder[2];
      out += kB64[(v >> 18) & 0x3F];
      out += kB64[(v >> 12) & 0x3F];
      out += kB64[(v >> 6) & 0x3F];
      out += kB64[v & 0x3F];
      remainderLen = 0;
    }
  }

  while (idx + 2 < len) {
    uint32_t v = ((uint32_t)data[idx] << 16) | ((uint32_t)data[idx + 1] << 8) | data[idx + 2];
    out += kB64[(v >> 18) & 0x3F];
    out += kB64[(v >> 12) & 0x3F];
    out += kB64[(v >> 6) & 0x3F];
    out += kB64[v & 0x3F];
    idx += 3;
  }

  remainderLen = 0;
  while (idx < len) {
    remainder[remainderLen++] = data[idx++];
  }
}

String GeminiASRChat::getRecognizedText() {
  return _recognizedText;
}

bool GeminiASRChat::hasNewResult() {
  return _hasNewResult;
}

void GeminiASRChat::clearResult() {
  _hasNewResult = false;
  _recognizedText = "";
}

void GeminiASRChat::setResultCallback(ResultCallback callback) {
  _resultCallback = callback;
}

void GeminiASRChat::setTimeoutNoSpeechCallback(TimeoutNoSpeechCallback callback) {
  _timeoutNoSpeechCallback = callback;
}
