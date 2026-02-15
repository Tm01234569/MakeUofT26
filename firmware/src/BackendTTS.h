#ifndef BackendTTS_h
#define BackendTTS_h

#include <Arduino.h>

class BackendTTS {
  public:
    BackendTTS();

    void setConfig(const char* baseUrl,
                   const char* apiKey,
                   const char* voiceId = "EST9Ui6982FZPSi7gCHi",
                   const char* modelId = "eleven_flash_v2_5",
                   const char* outputFormat = "mp3_22050_32");

    bool isConfigured() const;
    bool speak(const String& text);

  private:
    String _baseUrl;
    String _apiKey;
    String _voiceId;
    String _modelId;
    String _outputFormat;
};

#endif
