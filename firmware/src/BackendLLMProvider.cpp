#include "BackendLLMProvider.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

BackendLLMProvider::BackendLLMProvider(const char* baseUrl, const char* apiKey)
  : _baseUrl(baseUrl == nullptr ? "" : baseUrl),
    _apiKey(apiKey == nullptr ? "" : apiKey),
    _model("gemini-2.0-flash") {}

void BackendLLMProvider::setApiConfig(const char* baseUrl, const char* apiKey) {
  if (baseUrl != nullptr) _baseUrl = baseUrl;
  if (apiKey != nullptr) _apiKey = apiKey;
}

String BackendLLMProvider::sendMessage(const String& message) {
  DynamicJsonDocument doc(8192);
  doc["model"] = _model;
  doc["system_prompt"] = _systemPrompt;
  doc["user_message"] = message;

  if (_memoryEnabled && !_history.empty()) {
    JsonArray h = doc.createNestedArray("history");
    for (size_t i = 0; i < _history.size(); ++i) {
      JsonObject p = h.createNestedObject();
      p["user"] = _history[i].first;
      p["assistant"] = _history[i].second;
    }
  }

  String payload;
  serializeJson(doc, payload);
  String body = _postJson("/v1/llm/chat", payload);
  if (body.length() == 0) return "";

  DynamicJsonDocument out(8192);
  if (deserializeJson(out, body) != DeserializationError::Ok) {
    return "";
  }
  String text = out["text"].as<String>();
  text.trim();

  if (_memoryEnabled && text.length() > 0) {
    _history.push_back(std::make_pair(message, text));
    while (_history.size() > _maxHistoryPairs) {
      _history.erase(_history.begin());
    }
  }
  return text;
}

String BackendLLMProvider::sendVisionMessage(const uint8_t* imageData, size_t imageSize, const String& question, const char* mimeType) {
  String b64 = _base64Encode(imageData, imageSize);
  if (b64.length() == 0) return "";

  DynamicJsonDocument doc(16384);
  doc["model"] = _model;
  doc["system_prompt"] = _systemPrompt;
  doc["user_message"] = question;
  doc["image_base64"] = b64;
  doc["image_mime_type"] = mimeType == nullptr ? "image/jpeg" : mimeType;

  String payload;
  serializeJson(doc, payload);
  String body = _postJson("/v1/llm/chat", payload);
  if (body.length() == 0) return "";

  DynamicJsonDocument out(8192);
  if (deserializeJson(out, body) != DeserializationError::Ok) {
    return "";
  }
  String text = out["text"].as<String>();
  text.trim();
  return text;
}

void BackendLLMProvider::setSystemPrompt(const String& prompt) {
  _systemPrompt = prompt;
}

void BackendLLMProvider::enableMemory(bool enable) {
  _memoryEnabled = enable;
  if (!enable) clearMemory();
}

void BackendLLMProvider::clearMemory() {
  _history.clear();
}

void BackendLLMProvider::setModel(const String& model) {
  if (model.length() > 0) _model = model;
}

String BackendLLMProvider::getModel() const {
  return _model;
}

String BackendLLMProvider::getProviderName() const {
  return "backend";
}

String BackendLLMProvider::_postJson(const String& endpoint, const String& payload) {
  if (_baseUrl.length() == 0 || _apiKey.length() == 0) {
    return "";
  }
  HTTPClient http;
  String url = _baseUrl + endpoint;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", _apiKey);
  int code = http.POST(payload);
  String body = http.getString();
  http.end();
  if (code < 200 || code >= 300) {
    Serial.printf("[BackendLLM] HTTP %d: %s\n", code, body.c_str());
    return "";
  }
  return body;
}

String BackendLLMProvider::_base64Encode(const uint8_t* data, size_t len) const {
  static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t a = i < len ? data[i] : 0;
    uint32_t b = (i + 1) < len ? data[i + 1] : 0;
    uint32_t c = (i + 2) < len ? data[i + 2] : 0;
    uint32_t t = (a << 16) | (b << 8) | c;
    out += b64[(t >> 18) & 0x3F];
    out += b64[(t >> 12) & 0x3F];
    out += (i + 1) < len ? b64[(t >> 6) & 0x3F] : '=';
    out += (i + 2) < len ? b64[t & 0x3F] : '=';
  }
  return out;
}
