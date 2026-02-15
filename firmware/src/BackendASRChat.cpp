#include "BackendASRChat.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

BackendASRChat::BackendASRChat(const char* apiUrl, const char* apiKey)
    : _apiUrl(apiUrl == nullptr ? "" : apiUrl),
      _apiKey(apiKey == nullptr ? "" : apiKey) {}

void BackendASRChat::setApiConfig(const char* apiUrl, const char* apiKey) {
  if (apiUrl != nullptr) _apiUrl = apiUrl;
  if (apiKey != nullptr) _apiKey = apiKey;
}

void BackendASRChat::setAudioParams(int sampleRate, int bitsPerSample, int channels) {
  _sampleRate = sampleRate;
  _bitsPerSample = bitsPerSample;
  _channels = channels;
}

void BackendASRChat::setSilenceDuration(unsigned long duration) {
  _silenceDuration = duration;
}

void BackendASRChat::setMaxRecordingSeconds(int seconds) {
  if (seconds < 1) seconds = 1;
  if (seconds > 120) seconds = 120;
  _maxSeconds = seconds;
}

void BackendASRChat::setManualStopOnly(bool enable) {
  _manualStopOnly = enable;
}

bool BackendASRChat::initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin) {
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

bool BackendASRChat::connectWebSocket() {
  return true;
}

String BackendASRChat::_normalizedBaseUrl() const {
  String url = _apiUrl;
  while (url.endsWith("/")) {
    url.remove(url.length() - 1);
  }
  return url;
}

bool BackendASRChat::_startStreamSession() {
  String base = _normalizedBaseUrl();
  if (base.length() == 0) {
    Serial.println("[Backend ASR] Missing asr_api_url");
    return false;
  }

  DynamicJsonDocument doc(256);
  doc["sample_rate"] = _sampleRate;
  doc["channels"] = _channels;
  doc["bits"] = _bitsPerSample;

  String payload;
  serializeJson(doc, payload);

  HTTPClient http;
  String url = base + "/v1/asr/stream/start";
  if (!http.begin(url)) {
    Serial.println("[Backend ASR] start HTTP begin failed");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  if (_apiKey.length() > 0) {
    http.addHeader("x-api-key", _apiKey);
  }

  int code = http.POST(payload);
  String body = http.getString();
  http.end();
  if (code != 200) {
    Serial.printf("[Backend ASR] start HTTP %d: %s\n", code, body.c_str());
    return false;
  }

  DynamicJsonDocument out(512);
  if (deserializeJson(out, body) != DeserializationError::Ok) {
    Serial.println("[Backend ASR] start parse failed");
    return false;
  }

  _sessionId = out["session_id"].as<String>();
  _sessionId.trim();
  if (_sessionId.length() == 0) {
    Serial.println("[Backend ASR] start missing session_id");
    return false;
  }
  return true;
}

bool BackendASRChat::_sendStreamChunk(const uint8_t* data, size_t len) {
  if (_sessionId.length() == 0 || data == nullptr || len == 0) return false;

  String base = _normalizedBaseUrl();
  HTTPClient http;
  String url = base + "/v1/asr/stream/chunk?session_id=" + _sessionId;
  if (!http.begin(url)) {
    Serial.println("[Backend ASR] chunk HTTP begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/octet-stream");
  if (_apiKey.length() > 0) {
    http.addHeader("x-api-key", _apiKey);
  }

  int code = http.POST((uint8_t*)data, (int)len);
  if (code != 200) {
    String err = http.getString();
    Serial.printf("[Backend ASR] chunk HTTP %d: %s\n", code, err.c_str());
    http.end();
    return false;
  }

  http.end();
  return true;
}

bool BackendASRChat::_abortStreamSession() {
  if (_sessionId.length() == 0) return true;

  String sid = _sessionId;
  _sessionId = "";

  String base = _normalizedBaseUrl();
  HTTPClient http;
  String url = base + "/v1/asr/stream/abort?session_id=" + sid;
  if (!http.begin(url)) {
    return false;
  }
  if (_apiKey.length() > 0) {
    http.addHeader("x-api-key", _apiKey);
  }

  int code = http.POST("");
  http.end();
  return code == 200;
}

String BackendASRChat::_stopStreamSessionAndTranscribe() {
  if (_sessionId.length() == 0) return "";

  String sid = _sessionId;
  _sessionId = "";

  String base = _normalizedBaseUrl();
  HTTPClient http;
  String url = base + "/v1/asr/stream/stop?session_id=" + sid;
  if (!http.begin(url)) {
    Serial.println("[Backend ASR] stop HTTP begin failed");
    return "";
  }

  if (_apiKey.length() > 0) {
    http.addHeader("x-api-key", _apiKey);
  }

  int code = http.POST("");
  if (code != 200) {
    String err = http.getString();
    Serial.printf("[Backend ASR] stop HTTP %d: %s\n", code, err.c_str());
    http.end();
    return "";
  }

  String body = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    Serial.println("[Backend ASR] stop parse failed");
    return "";
  }
  String text = doc["text"].as<String>();
  text.trim();
  return text;
}

bool BackendASRChat::startRecording() {
  if (!_micInitialized) {
    Serial.println("[Backend ASR] Microphone not initialized");
    return false;
  }
  if (_apiUrl.length() == 0) {
    Serial.println("[Backend ASR] Missing asr_api_url");
    return false;
  }

  if (!_startStreamSession()) {
    return false;
  }

  _txChunkLen = 0;
  _totalSamples = 0;
  _hasSpeech = false;
  _hasNewResult = false;
  _pendingFinalize = false;
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

void BackendASRChat::stopRecording() {
  _isRecording = false;
  _pendingFinalize = false;
  _txChunkLen = 0;
  _totalSamples = 0;
  _hasSpeech = false;
  _abortStreamSession();
}

bool BackendASRChat::finalizeRecording() {
  if (!_isRecording) return false;
  _isRecording = false;
  _pendingFinalize = true;
  return true;
}

bool BackendASRChat::isRecording() {
  return _isRecording;
}

String BackendASRChat::_finalizeCurrentRecording() {
  if (_txChunkLen > 0) {
    if (!_sendStreamChunk(_txChunk, _txChunkLen)) {
      return "";
    }
    _txChunkLen = 0;
  }

  if (!_hasSpeech || _totalSamples < (size_t)(_sampleRate / 8)) {
    Serial.println("[Backend ASR] No speech detected");
    if (_timeoutNoSpeechCallback != nullptr) _timeoutNoSpeechCallback();
    _abortStreamSession();
    return "";
  }

  String result = _stopStreamSessionAndTranscribe();
  if (result.length() == 0) {
    Serial.println("[Backend ASR] Empty transcription result");
    return "";
  }

  _recognizedText = result;
  _hasNewResult = true;
  if (_resultCallback != nullptr) _resultCallback(result);
  return result;
}

void BackendASRChat::loop() {
  if (_pendingFinalize) {
    _pendingFinalize = false;
    _finalizeCurrentRecording();
    return;
  }

  if (!_isRecording) return;

  unsigned long now = millis();
  if (now - _lastDotMs > 1000) {
    Serial.print(".");
    _lastDotMs = now;
  }

  for (int i = 0; i < _samplesPerRead; ++i) {
    if (!_I2S.available()) break;

    int sample = _I2S.read();
    int16_t s = (int16_t)sample;

    if (_txChunkLen + 2 > sizeof(_txChunk)) {
      if (!_sendStreamChunk(_txChunk, _txChunkLen)) {
        _isRecording = false;
        _pendingFinalize = false;
        _abortStreamSession();
        return;
      }
      _txChunkLen = 0;
    }

    _txChunk[_txChunkLen++] = (uint8_t)(s & 0xFF);
    _txChunk[_txChunkLen++] = (uint8_t)((s >> 8) & 0xFF);
    _totalSamples++;

    if (abs((int)s) >= _speechThreshold) {
      _hasSpeech = true;
      _lastSpeechMs = now;
    }
  }

  bool timedOut = !_manualStopOnly && ((now - _recordingStartMs) >= (unsigned long)(_maxSeconds * 1000));
  bool silenceDone = !_manualStopOnly && _hasSpeech && ((now - _lastSpeechMs) >= _silenceDuration) && (_totalSamples > (size_t)(_sampleRate / 5));
  bool noSpeechTimeout = !_manualStopOnly && !_hasSpeech && ((now - _recordingStartMs) >= 5000UL);

  if (!timedOut && !silenceDone && !noSpeechTimeout) return;

  _isRecording = false;
  Serial.println();
  _finalizeCurrentRecording();
}

String BackendASRChat::getRecognizedText() {
  return _recognizedText;
}

bool BackendASRChat::hasNewResult() {
  return _hasNewResult;
}

void BackendASRChat::clearResult() {
  _hasNewResult = false;
  _recognizedText = "";
}

void BackendASRChat::setResultCallback(ResultCallback callback) {
  _resultCallback = callback;
}

void BackendASRChat::setTimeoutNoSpeechCallback(TimeoutNoSpeechCallback callback) {
  _timeoutNoSpeechCallback = callback;
}
