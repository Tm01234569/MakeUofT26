#ifndef GeminiASRChat_h
#define GeminiASRChat_h

#include <Arduino.h>
#include <ESP_I2S.h>

class GeminiASRChat {
  public:
    typedef void (*ResultCallback)(String text);
    typedef void (*TimeoutNoSpeechCallback)();

    GeminiASRChat(const char* apiKey = nullptr,
                  const char* model = "gemini-2.0-flash",
                  const char* baseUrl = "https://generativelanguage.googleapis.com");

    void setApiConfig(const char* apiKey = nullptr, const char* model = nullptr, const char* baseUrl = nullptr);
    void setModel(const char* model);

    bool initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin);
    void setAudioParams(int sampleRate = 16000, int bitsPerSample = 16, int channels = 1);
    void setSilenceDuration(unsigned long duration);
    void setMaxRecordingSeconds(int seconds);

    bool connectWebSocket();  // Compatibility no-op
    bool startRecording();
    void stopRecording();
    bool isRecording();
    void loop();

    String getRecognizedText();
    bool hasNewResult();
    void clearResult();

    void setResultCallback(ResultCallback callback);
    void setTimeoutNoSpeechCallback(TimeoutNoSpeechCallback callback);

  private:
    String _apiKey;
    String _model;
    String _baseUrl;

    I2SClass _I2S;
    bool _micInitialized = false;

    int _sampleRate = 16000;
    int _bitsPerSample = 16;
    int _channels = 1;
    int _samplesPerRead = 800;
    unsigned long _silenceDuration = 900;
    int _maxSeconds = 5;
    int _speechThreshold = 120;

    int16_t* _pcmBuffer = nullptr;
    size_t _pcmCapacitySamples = 0;
    size_t _pcmSamples = 0;

    bool _isRecording = false;
    bool _hasSpeech = false;
    bool _hasNewResult = false;
    String _recognizedText = "";
    unsigned long _recordingStartMs = 0;
    unsigned long _lastSpeechMs = 0;
    unsigned long _lastDotMs = 0;

    ResultCallback _resultCallback = nullptr;
    TimeoutNoSpeechCallback _timeoutNoSpeechCallback = nullptr;

    bool ensurePcmBuffer();
    String transcribeCurrentBuffer();
    String postTranscription(const String& payload);
    String extractTextFromResponse(const String& response) const;
    String buildPayloadFromPcm16(const int16_t* pcm, size_t samples) const;
    String buildWavBase64FromPcm16(const int16_t* pcm, size_t samples) const;
    static void appendBase64Bytes(String& out,
                                  const uint8_t* data,
                                  size_t len,
                                  uint8_t remainder[3],
                                  uint8_t& remainderLen);
};

#endif
