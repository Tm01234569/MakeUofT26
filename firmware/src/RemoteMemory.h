#ifndef RemoteMemory_h
#define RemoteMemory_h

#include <Arduino.h>

class RemoteMemory {
  public:
    RemoteMemory();
    RemoteMemory(const char* baseUrl, const char* apiKey, const char* deviceId = nullptr);

    void setConfig(const char* baseUrl, const char* apiKey, const char* deviceId = nullptr);
    void setEnabled(bool enabled);
    bool isEnabled() const;

    bool storeConversation(const String& userMessage,
                           const String& assistantMessage,
                           const String& aiProvider,
                           const String& visualContext = "");

    bool storeVisualEvent(const String& description,
                          const String& eventType = "observation");

    String recall(const String& query, int topConversations = 5, int topVisualEvents = 3);

  private:
    String _baseUrl;
    String _apiKey;
    String _deviceId;
    bool _enabled;

    bool _postJson(const String& endpoint, const String& payload, String* response = nullptr);
};

#endif
