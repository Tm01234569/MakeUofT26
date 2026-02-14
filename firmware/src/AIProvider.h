#ifndef AIProvider_h
#define AIProvider_h

#include <Arduino.h>

class AIProvider {
  public:
    virtual ~AIProvider() {}
    virtual String sendMessage(const String& message) = 0;
    virtual String sendVisionMessage(const uint8_t* imageData, size_t imageSize, const String& question, const char* mimeType = "image/jpeg") = 0;
    virtual void setSystemPrompt(const String& prompt) = 0;
    virtual void enableMemory(bool enable) = 0;
    virtual void clearMemory() = 0;
    virtual void setModel(const String& model) = 0;
    virtual String getModel() const = 0;
    virtual String getProviderName() const = 0;
};

#endif
