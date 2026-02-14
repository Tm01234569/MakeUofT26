#ifndef OpenAIProvider_h
#define OpenAIProvider_h

#include <Arduino.h>
#include "AIProvider.h"
#include "ArduinoGPTChat.h"

class OpenAIProvider : public AIProvider {
  public:
    OpenAIProvider(const char* apiKey = nullptr, const char* apiBaseUrl = "https://api.openai.com");

    void setApiConfig(const char* apiKey = nullptr, const char* apiBaseUrl = nullptr);

    String sendMessage(const String& message) override;
    String sendVisionMessage(const uint8_t* imageData, size_t imageSize, const String& question, const char* mimeType = "image/jpeg") override;
    void setSystemPrompt(const String& prompt) override;
    void enableMemory(bool enable) override;
    void clearMemory() override;
    void setModel(const String& model) override;
    String getModel() const override;
    String getProviderName() const override;

    ArduinoGPTChat& client();

  private:
    ArduinoGPTChat _chat;
};

#endif
