#include "VisualContextManager.h"

VisualContextManager::VisualContextManager(AIProvider* provider)
  : _provider(provider),
    _captureCb(nullptr),
    _releaseCb(nullptr),
    _defaultPrompt("Briefly describe what you see"),
    _lastUpdateMs(0) {}

void VisualContextManager::setProvider(AIProvider* provider) {
  _provider = provider;
}

void VisualContextManager::setCaptureCallbacks(CaptureJpegCallback captureCb, ReleaseJpegCallback releaseCb) {
  _captureCb = captureCb;
  _releaseCb = releaseCb;
}

void VisualContextManager::setPrompt(const String& prompt) {
  if (prompt.length() > 0) {
    _defaultPrompt = prompt;
  }
}

String VisualContextManager::captureAndDescribe(const String& prompt) {
  if (_provider == nullptr || _captureCb == nullptr) {
    return "";
  }

  uint8_t* jpeg = nullptr;
  size_t jpegSize = 0;
  if (!_captureCb(&jpeg, &jpegSize) || jpeg == nullptr || jpegSize == 0) {
    return "";
  }

  String p = prompt.length() > 0 ? prompt : _defaultPrompt;
  String ctx = _provider->sendVisionMessage(jpeg, jpegSize, p, "image/jpeg");

  if (_releaseCb != nullptr) {
    _releaseCb(jpeg);
  }

  if (ctx.length() > 0) {
    _cachedContext = ctx;
    _lastUpdateMs = millis();
  }

  return ctx;
}

void VisualContextManager::refreshContextAsync() {
  // Cooperative single-threaded fallback for Arduino loops.
  captureAndDescribe(_defaultPrompt);
}

bool VisualContextManager::isContextStale(unsigned long maxAgeMs) const {
  if (_lastUpdateMs == 0) {
    return true;
  }
  return (millis() - _lastUpdateMs) > maxAgeMs;
}

String VisualContextManager::getCachedContext() const {
  return _cachedContext;
}

unsigned long VisualContextManager::getLastUpdateMs() const {
  return _lastUpdateMs;
}
