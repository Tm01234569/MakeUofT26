#ifndef VisualContextManager_h
#define VisualContextManager_h

#include <Arduino.h>
#include "AIProvider.h"

class VisualContextManager {
  public:
    typedef bool (*CaptureJpegCallback)(uint8_t** outData, size_t* outSize);
    typedef void (*ReleaseJpegCallback)(uint8_t* data);

    VisualContextManager(AIProvider* provider = nullptr);

    void setProvider(AIProvider* provider);
    void setCaptureCallbacks(CaptureJpegCallback captureCb, ReleaseJpegCallback releaseCb);
    void setPrompt(const String& prompt);

    String captureAndDescribe(const String& prompt = "Briefly describe what you see");
    void refreshContextAsync();
    bool isContextStale(unsigned long maxAgeMs = 30000) const;
    String getCachedContext() const;
    unsigned long getLastUpdateMs() const;

  private:
    AIProvider* _provider;
    CaptureJpegCallback _captureCb;
    ReleaseJpegCallback _releaseCb;
    String _defaultPrompt;
    String _cachedContext;
    unsigned long _lastUpdateMs;
};

#endif
