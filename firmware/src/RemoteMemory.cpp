#include "RemoteMemory.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

RemoteMemory::RemoteMemory()
  : _enabled(false) {}

RemoteMemory::RemoteMemory(const char* baseUrl, const char* apiKey, const char* deviceId)
  : _baseUrl(baseUrl == nullptr ? "" : baseUrl),
    _apiKey(apiKey == nullptr ? "" : apiKey),
    _deviceId(deviceId == nullptr ? "" : deviceId),
    _enabled(true) {}

void RemoteMemory::setConfig(const char* baseUrl, const char* apiKey, const char* deviceId) {
  _baseUrl = baseUrl == nullptr ? "" : baseUrl;
  _apiKey = apiKey == nullptr ? "" : apiKey;
  if (deviceId != nullptr) {
    _deviceId = deviceId;
  }
}

void RemoteMemory::setEnabled(bool enabled) {
  _enabled = enabled;
}

bool RemoteMemory::isEnabled() const {
  return _enabled && _baseUrl.length() > 0 && _apiKey.length() > 0;
}

bool RemoteMemory::storeConversation(const String& userMessage,
                                     const String& assistantMessage,
                                     const String& aiProvider,
                                     const String& visualContext) {
  if (!isEnabled()) {
    return false;
  }

  DynamicJsonDocument doc(2048);
  doc["device_id"] = _deviceId;
  doc["user_message"] = userMessage;
  doc["assistant_message"] = assistantMessage;
  doc["ai_provider"] = aiProvider;
  if (visualContext.length() > 0) {
    doc["visual_context"] = visualContext;
  }

  String payload;
  serializeJson(doc, payload);
  return _postJson("/v1/memory/conversations", payload, nullptr);
}

bool RemoteMemory::storeVisualEvent(const String& description,
                                    const String& eventType) {
  if (!isEnabled()) {
    return false;
  }

  DynamicJsonDocument doc(1024);
  doc["device_id"] = _deviceId;
  doc["description"] = description;
  doc["event_type"] = eventType;

  String payload;
  serializeJson(doc, payload);
  return _postJson("/v1/memory/visual-events", payload, nullptr);
}

String RemoteMemory::recall(const String& query, int topConversations, int topVisualEvents) {
  if (!isEnabled()) {
    return "";
  }

  DynamicJsonDocument doc(512);
  doc["query"] = query;
  doc["device_id"] = _deviceId;
  doc["top_conversations"] = topConversations;
  doc["top_visual_events"] = topVisualEvents;

  String payload;
  serializeJson(doc, payload);

  String response;
  if (!_postJson("/v1/memory/recall", payload, &response)) {
    return "";
  }
  return response;
}

bool RemoteMemory::_postJson(const String& endpoint,
                             const String& payload,
                             String* response) {
  HTTPClient http;
  String url = _baseUrl + endpoint;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-api-key", _apiKey);
  http.addHeader("Authorization", "Bearer " + _apiKey);
  http.addHeader("x-device-id", _deviceId);

  int code = http.POST(payload);
  String body = http.getString();
  http.end();

  if (response != nullptr) {
    *response = body;
  }

  if (code >= 200 && code < 300) {
    return true;
  }

  Serial.printf("[RemoteMemory] POST %s failed: %d %s\n", endpoint.c_str(), code, body.c_str());
  return false;
}
