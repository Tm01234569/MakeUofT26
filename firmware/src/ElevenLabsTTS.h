#ifndef ElevenLabsTTS_h
#define ElevenLabsTTS_h

#include <Arduino.h>

class ElevenLabsTTS {
  public:
    ElevenLabsTTS();

    void setConfig(const char* apiKey,
                   const char* voiceId,
                   const char* modelId = "eleven_flash_v2_5",
                   const char* outputFormat = "mp3_22050_32");

    bool isConfigured() const;
    bool speak(const String& text);

  private:
    String _apiKey;
    String _voiceId;
    String _modelId;
    String _outputFormat;
};

#endif
