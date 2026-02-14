#ifndef GeminiProvider_h
#define GeminiProvider_h

#include <Arduino.h>
#include <HTTPClient.h>
#include <vector>
#include "AIProvider.h"

class GeminiProvider : public AIProvider {
  public:
    GeminiProvider(const char* apiKey = nullptr, const char* baseUrl = "https://generativelanguage.googleapis.com");

    void setApiConfig(const char* apiKey = nullptr, const char* baseUrl = nullptr);

    String sendMessage(const String& message) override;
    String sendVisionMessage(const uint8_t* imageData, size_t imageSize, const String& question, const char* mimeType = "image/jpeg") override;
    void setSystemPrompt(const String& prompt) override;
    void enableMemory(bool enable) override;
    void clearMemory() override;
    void setModel(const String& model) override;
    String getModel() const override;
    String getProviderName() const override;

  private:
    String _apiKey;
    String _baseUrl;
    String _model;
    String _systemPrompt;
    bool _memoryEnabled = true;
    size_t _maxHistoryPairs = 5;
    std::vector<std::pair<String, String>> _history;

    String _buildEndpoint(bool stream = false) const;
    String _postJson(const String& endpointPath, const String& payload);
    String _extractTextFromResponse(const String& response) const;
    String _buildPayload(const String& userMessage) const;
    String _buildVisionPayload(const String& question, const String& base64Image, const char* mimeType) const;
    String _base64Encode(const uint8_t* data, size_t len) const;
};

#endif
