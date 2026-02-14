#include "GeminiProvider.h"
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

GeminiProvider::GeminiProvider(const char* apiKey, const char* baseUrl)
  : _apiKey(apiKey == nullptr ? "" : apiKey),
    _baseUrl(baseUrl == nullptr ? "https://generativelanguage.googleapis.com" : baseUrl),
    _model("gemini-2.0-flash") {}

void GeminiProvider::setApiConfig(const char* apiKey, const char* baseUrl) {
  if (apiKey != nullptr) {
    _apiKey = apiKey;
  }
  if (baseUrl != nullptr) {
    _baseUrl = baseUrl;
  }
}

String GeminiProvider::sendMessage(const String& message) {
  String payload = _buildPayload(message);
  String response = _postJson(_buildEndpoint(false), payload);
  String text = _extractTextFromResponse(response);

  if (_memoryEnabled && text.length() > 0) {
    _history.push_back(std::make_pair(message, text));
    while (_history.size() > _maxHistoryPairs) {
      _history.erase(_history.begin());
    }
  }

  return text;
}

String GeminiProvider::sendVisionMessage(const uint8_t* imageData, size_t imageSize, const String& question, const char* mimeType) {
  String b64 = _base64Encode(imageData, imageSize);
  if (b64.length() == 0) {
    return "";
  }

  String payload = _buildVisionPayload(question, b64, mimeType);
  String response = _postJson(_buildEndpoint(false), payload);
  return _extractTextFromResponse(response);
}

void GeminiProvider::setSystemPrompt(const String& prompt) {
  _systemPrompt = prompt;
}

void GeminiProvider::enableMemory(bool enable) {
  _memoryEnabled = enable;
  if (!enable) {
    clearMemory();
  }
}

void GeminiProvider::clearMemory() {
  _history.clear();
}

void GeminiProvider::setModel(const String& model) {
  if (model.length() > 0) {
    _model = model;
  }
}

String GeminiProvider::getModel() const {
  return _model;
}

String GeminiProvider::getProviderName() const {
  return "gemini";
}

String GeminiProvider::_buildEndpoint(bool stream) const {
  String path = "/v1beta/models/" + _model;
  path += stream ? ":streamGenerateContent" : ":generateContent";
  path += "?key=" + _apiKey;
  return path;
}

String GeminiProvider::_postJson(const String& endpointPath, const String& payload) {
  if (_apiKey.length() == 0) {
    return "";
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = _baseUrl + endpointPath;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  if (code != 200) {
    String err = http.getString();
    http.end();
    Serial.printf("[Gemini] HTTP %d: %s\n", code, err.c_str());
    return "";
  }

  String response = http.getString();
  http.end();
  return response;
}

String GeminiProvider::_extractTextFromResponse(const String& response) const {
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, response) != DeserializationError::Ok) {
    return "";
  }

  if (!doc["candidates"].is<JsonArray>() || doc["candidates"].size() == 0) {
    return "";
  }

  JsonVariant parts = doc["candidates"][0]["content"]["parts"];
  if (!parts.is<JsonArray>() || parts.size() == 0) {
    return "";
  }

  String out = parts[0]["text"].as<String>();
  out.replace("\n", " ");
  out.replace("\r", "");
  return out;
}

String GeminiProvider::_buildPayload(const String& userMessage) const {
  DynamicJsonDocument doc(8192);

  if (_systemPrompt.length() > 0) {
    JsonObject sys = doc.createNestedObject("systemInstruction");
    JsonArray parts = sys.createNestedArray("parts");
    JsonObject part = parts.createNestedObject();
    part["text"] = _systemPrompt;
  }

  JsonArray contents = doc.createNestedArray("contents");

  if (_memoryEnabled) {
    for (size_t i = 0; i < _history.size(); i++) {
      JsonObject user = contents.createNestedObject();
      user["role"] = "user";
      JsonArray userParts = user.createNestedArray("parts");
      JsonObject userText = userParts.createNestedObject();
      userText["text"] = _history[i].first;

      JsonObject model = contents.createNestedObject();
      model["role"] = "model";
      JsonArray modelParts = model.createNestedArray("parts");
      JsonObject modelText = modelParts.createNestedObject();
      modelText["text"] = _history[i].second;
    }
  }

  JsonObject current = contents.createNestedObject();
  current["role"] = "user";
  JsonArray currentParts = current.createNestedArray("parts");
  JsonObject currentText = currentParts.createNestedObject();
  currentText["text"] = userMessage;

  JsonObject generationConfig = doc.createNestedObject("generationConfig");
  generationConfig["temperature"] = 0.8;
  generationConfig["maxOutputTokens"] = 256;

  String payload;
  serializeJson(doc, payload);
  return payload;
}

String GeminiProvider::_buildVisionPayload(const String& question, const String& base64Image, const char* mimeType) const {
  DynamicJsonDocument doc(16384);

  if (_systemPrompt.length() > 0) {
    JsonObject sys = doc.createNestedObject("systemInstruction");
    JsonArray parts = sys.createNestedArray("parts");
    JsonObject part = parts.createNestedObject();
    part["text"] = _systemPrompt;
  }

  JsonArray contents = doc.createNestedArray("contents");
  JsonObject user = contents.createNestedObject();
  user["role"] = "user";
  JsonArray parts = user.createNestedArray("parts");

  JsonObject textPart = parts.createNestedObject();
  textPart["text"] = question;

  JsonObject imgPart = parts.createNestedObject();
  JsonObject inlineData = imgPart.createNestedObject("inline_data");
  inlineData["mime_type"] = mimeType == nullptr ? "image/jpeg" : mimeType;
  inlineData["data"] = base64Image;

  String payload;
  serializeJson(doc, payload);
  return payload;
}

String GeminiProvider::_base64Encode(const uint8_t* data, size_t len) const {
  static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  out.reserve((len + 2) / 3 * 4);

  for (size_t i = 0; i < len; i += 3) {
    uint32_t octet_a = i < len ? data[i] : 0;
    uint32_t octet_b = (i + 1) < len ? data[i + 1] : 0;
    uint32_t octet_c = (i + 2) < len ? data[i + 2] : 0;

    uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;
    out += b64[(triple >> 18) & 0x3F];
    out += b64[(triple >> 12) & 0x3F];
    out += (i + 1) < len ? b64[(triple >> 6) & 0x3F] : '=';
    out += (i + 2) < len ? b64[triple & 0x3F] : '=';
  }

  return out;
}
