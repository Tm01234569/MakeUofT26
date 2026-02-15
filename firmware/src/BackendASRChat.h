#ifndef BackendASRChat_h
#define BackendASRChat_h

#include <Arduino.h>
#include <ESP_I2S.h>

class BackendASRChat {
  public:
    typedef void (*ResultCallback)(String text);
    typedef void (*TimeoutNoSpeechCallback)();

    BackendASRChat(const char* apiUrl = nullptr, const char* apiKey = nullptr);

    void setApiConfig(const char* apiUrl = nullptr, const char* apiKey = nullptr);
    void setAudioParams(int sampleRate = 16000, int bitsPerSample = 16, int channels = 1);
    void setSilenceDuration(unsigned long duration);
    void setMaxRecordingSeconds(int seconds);
    void setManualStopOnly(bool enable);

    bool initINMP441Microphone(int i2sSckPin, int i2sWsPin, int i2sSdPin);
    bool connectWebSocket();  // Compatibility no-op
    bool startRecording();
    void stopRecording();
    bool finalizeRecording();
    bool isRecording();
    void loop();

    String getRecognizedText();
    bool hasNewResult();
    void clearResult();

    void setResultCallback(ResultCallback callback);
    void setTimeoutNoSpeechCallback(TimeoutNoSpeechCallback callback);

  private:
    String _apiUrl;
    String _apiKey;

    I2SClass _I2S;
    bool _micInitialized = false;

    int _sampleRate = 16000;
    int _bitsPerSample = 16;
    int _channels = 1;
    int _samplesPerRead = 800;
    unsigned long _silenceDuration = 900;
    int _maxSeconds = 2;
    int _speechThreshold = 120;

    uint8_t _txChunk[4096];
    size_t _txChunkLen = 0;
    size_t _totalSamples = 0;
    String _sessionId = "";

    bool _isRecording = false;
    bool _pendingFinalize = false;
    bool _manualStopOnly = false;
    bool _hasSpeech = false;
    bool _hasNewResult = false;
    String _recognizedText = "";
    unsigned long _recordingStartMs = 0;
    unsigned long _lastSpeechMs = 0;
    unsigned long _lastDotMs = 0;

    ResultCallback _resultCallback = nullptr;
    TimeoutNoSpeechCallback _timeoutNoSpeechCallback = nullptr;

    String _normalizedBaseUrl() const;
    bool _startStreamSession();
    bool _sendStreamChunk(const uint8_t* data, size_t len);
    bool _abortStreamSession();
    String _stopStreamSessionAndTranscribe();
    String _finalizeCurrentRecording();
};

#endif
