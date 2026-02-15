#ifndef BackendLLMProvider_h
#define BackendLLMProvider_h

#include <Arduino.h>
#include <vector>
#include "AIProvider.h"

class BackendLLMProvider : public AIProvider {
  public:
    BackendLLMProvider(const char* baseUrl = nullptr, const char* apiKey = nullptr);

    void setApiConfig(const char* baseUrl = nullptr, const char* apiKey = nullptr);

    String sendMessage(const String& message) override;
    String sendVisionMessage(const uint8_t* imageData, size_t imageSize, const String& question, const char* mimeType = "image/jpeg") override;
    void setSystemPrompt(const String& prompt) override;
    void enableMemory(bool enable) override;
    void clearMemory() override;
    void setModel(const String& model) override;
    String getModel() const override;
    String getProviderName() const override;

  private:
    String _baseUrl;
    String _apiKey;
    String _model;
    String _systemPrompt;
    bool _memoryEnabled = true;
    size_t _maxHistoryPairs = 5;
    std::vector<std::pair<String, String>> _history;

    String _postJson(const String& endpoint, const String& payload);
    String _base64Encode(const uint8_t* data, size_t len) const;
};

#endif
