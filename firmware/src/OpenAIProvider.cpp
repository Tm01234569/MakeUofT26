#include "OpenAIProvider.h"
#include <SPIFFS.h>

OpenAIProvider::OpenAIProvider(const char* apiKey, const char* apiBaseUrl)
  : _chat(apiKey, apiBaseUrl) {}

void OpenAIProvider::setApiConfig(const char* apiKey, const char* apiBaseUrl) {
  _chat.setApiConfig(apiKey, apiBaseUrl);
}

String OpenAIProvider::sendMessage(const String& message) {
  return _chat.sendMessage(message);
}

String OpenAIProvider::sendVisionMessage(const uint8_t* imageData, size_t imageSize, const String& question, const char* mimeType) {
  (void)mimeType;
  if (!SPIFFS.begin(true)) {
    return "";
  }

  const char* tempPath = "/vision_input.bin";
  File f = SPIFFS.open(tempPath, FILE_WRITE);
  if (!f) {
    return "";
  }

  size_t written = f.write(imageData, imageSize);
  f.close();

  if (written != imageSize) {
    SPIFFS.remove(tempPath);
    return "";
  }

  String result = _chat.sendImageMessage(tempPath, question);
  SPIFFS.remove(tempPath);
  return result;
}

void OpenAIProvider::setSystemPrompt(const String& prompt) {
  _chat.setSystemPrompt(prompt.c_str());
}

void OpenAIProvider::enableMemory(bool enable) {
  _chat.enableMemory(enable);
}

void OpenAIProvider::clearMemory() {
  _chat.clearMemory();
}

void OpenAIProvider::setModel(const String& model) {
  _chat.setChatModel(model.c_str());
}

String OpenAIProvider::getModel() const {
  return _chat.getChatModel();
}

String OpenAIProvider::getProviderName() const {
  return "openai";
}

ArduinoGPTChat& OpenAIProvider::client() {
  return _chat;
}
