/*
 * ============================================================================
 * ESP32 Configurable Voice Assistant System
 * ============================================================================
 * Features: Choose between Free or Pro TTS via serial JSON config
 * - Free version (subscription="free"): Uses OpenAI TTS
 * - Pro version (subscription="pro"): Uses MiniMax TTS
 *
 * Configuration Method:
 * 1. After power-on, send JSON config via serial before pressing BOOT button
 * 2. After config is complete, press BOOT button to start voice assistant
 *
 * Free version config example:
 * {"wifi_ssid":"YourWiFi","wifi_password":"YourPassword","subscription":"free","asr_api_key":"your-asr-key","asr_cluster":"volcengine_input_en","openai_apiKey":"your-openai-key","openai_apiBaseUrl":"https://api.openai.com","system_prompt":"You are a helpful assistant."}
 *
 * Pro version config example:
 * {"wifi_ssid":"YourWiFi","wifi_password":"YourPassword","subscription":"pro","asr_api_key":"your-asr-key","asr_cluster":"volcengine_input_en","openai_apiKey":"your-openai-key","openai_apiBaseUrl":"https://api.openai.com","system_prompt":"You are a helpful assistant.","minimax_apiKey":"your-minimax-key","minimax_groupId":"your-group-id","tts_voice_id":"female-tianmei"}
 *
 * Hardware Requirements:
 * - ESP32 development board
 * - INMP441 MEMS microphone module
 * - I2S audio output device (speaker/amplifier)
 * ============================================================================
 */

#include <WiFi.h>
#include <ArduinoASRChat.h>
#include <ArduinoGPTChat.h>
#include <ArduinoTTSChat.h>
#include <AIProvider.h>
#include <OpenAIProvider.h>
#include <GeminiProvider.h>
#include <GeminiASRChat.h>
#include <BackendASRChat.h>
#include <BackendLLMProvider.h>
#include <BackendTTS.h>
#include <VisualContextManager.h>
#include <RemoteMemory.h>
#include <ElevenLabsTTS.h>
#include <WebControl.h>
#include <OpenAIVisionProxy.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <ctype.h>
#include "Audio.h"

// ============================================================================
// Hardware Pin Definitions
// ============================================================================

// I2S audio output pins (ESP32 classic + MAX98357)
#define I2S_DOUT 22  // DIN
#define I2S_BCLK 26  // BCLK
#define I2S_LRC 25   // LRC/WS

// INMP441 microphone input pin definitions
// NOTE: GPIO6-11 are connected to flash on ESP32 classic, do not use them.
#define I2S_MIC_SERIAL_CLOCK 32      // SCK - Serial clock
#define I2S_MIC_LEFT_RIGHT_CLOCK 33  // WS - Left/Right channel clock
#define I2S_MIC_SERIAL_DATA 34       // SD - Serial data (input-only pin is OK here)

// BOOT button pin
#define BOOT_BUTTON_PIN 0

// Recording sample rate
#define SAMPLE_RATE 16000

// ============================================================================
// Configuration Variables
// ============================================================================

// WiFi configuration
String wifi_ssid = "";
String wifi_password = "";

// Subscription type: "free" or "pro"
String subscription = "free";

// ASR configuration
String asr_provider = "bytedance";  // bytedance|gemini|backend
String asr_api_key = "";
String asr_cluster = "volcengine_input_en";
String asr_api_url = "";

// OpenAI configuration
String openai_apiKey = "";
String openai_apiBaseUrl = "";
String openai_model = "gpt-4.1-nano";

// Dedicated TTS configuration (lets chat use OpenRouter while TTS stays OpenAI)
String tts_apiKey = "";
String tts_apiBaseUrl = "https://api.openai.com";
String tts_openai_model = "gpt-4o-mini-tts";
String tts_openai_voice = "alloy";
String tts_openai_speed = "1.0";
bool use_elevenlabs_tts = true;
String elevenlabs_api_key = "";
String elevenlabs_voice_id = "EST9Ui6982FZPSi7gCHi";
String elevenlabs_model_id = "eleven_flash_v2_5";
String elevenlabs_output_format = "mp3_22050_32";

// Gemini configuration
String gemini_apiKey = "";
String gemini_model = "gemini-2.0-flash";
String ai_provider = "openai";
String backend_api_url = "";
String backend_api_key = "";
bool use_backend_tts = true;

// System prompt
String system_prompt = "You are a helpful AI assistant.";

// Vision context configuration
bool vision_enabled = false;
unsigned long vision_refresh_interval = 60000;
bool vision_on_conversation_start = true;
bool vision_capture_on_user_turn = true;
String vision_prompt = "Briefly describe what you see";
int vision_dedupe_threshold_pct = 90;               // 0..100, higher = stricter dedupe
unsigned long vision_min_store_interval_ms = 30000; // min time between stored visual events
int vision_max_events_per_hour = 30;                // write-rate guard

// Remote long-term memory (Mongo-backed API recommended)
String memory_api_url = "";
String memory_api_key = "";
String device_id = "";
String memory_mode = "local";  // local|remote|both
bool memory_store_visual_events = true;

// Interaction mode
// true  -> one utterance per "start" command/button press
// false -> continuous auto-restart conversation loop
bool single_turn_mode = true;
bool manual_record_control = true;

// Web control
bool web_control_enabled = false;
int web_port = 80;
int web_ws_port = 81;

// MiniMax TTS configuration (Pro version only)
String minimax_apiKey = "";
String minimax_groupId = "";
String tts_voice_id = "female-tianmei";
float tts_speed = 1.0;
float tts_volume = 1.0;
String tts_model = "speech-2.6-hd";
String tts_audio_format = "mp3";
int tts_sample_rate = 16000;  // WebSocket TTS: 16000 Hz for best quality
int tts_bitrate = 32000;      // WebSocket TTS: 32000 bps minimum

// Configuration status
bool configReceived = false;
bool systemInitialized = false;

// ============================================================================
// Global Object Instances
// ============================================================================

Audio audio;
ArduinoASRChat* asrChat = nullptr;
GeminiASRChat* geminiAsrChat = nullptr;
BackendASRChat* backendAsrChat = nullptr;
OpenAIProvider* openaiProvider = nullptr;
OpenAIProvider* ttsProvider = nullptr;
GeminiProvider* geminiProvider = nullptr;
BackendLLMProvider* backendProvider = nullptr;
AIProvider* aiProvider = nullptr;
ArduinoTTSChat* ttsChat = nullptr;  // WebSocket-based TTS for Pro version
VisualContextManager* visualContextMgr = nullptr;
WebControl* webControl = nullptr;
RemoteMemory remoteMemory;
ElevenLabsTTS elevenlabsTTS;
BackendTTS backendTTS;
Preferences preferences;

// TTS completion flag for WebSocket mode
volatile bool ttsCompleted = false;

// ============================================================================
// State Machine Definition
// ============================================================================

enum ConversationState {
  STATE_WAITING_CONFIG,    // Waiting for config state
  STATE_IDLE,              // Idle state
  STATE_LISTENING,         // Listening state
  STATE_PROCESSING_LLM,    // Processing state
  STATE_PLAYING_TTS,       // Playing state
  STATE_WAIT_TTS_COMPLETE  // Waiting for playback completion state
};

// ============================================================================
// State Variables
// ============================================================================

ConversationState currentState = STATE_WAITING_CONFIG;
bool continuousMode = false;
bool buttonPressed = false;
bool wasButtonPressed = false;
unsigned long ttsStartTime = 0;
unsigned long ttsCheckTime = 0;
unsigned long lastVisionRefresh = 0;
String lastVisualContext = "";
String lastVisionReason = "none";
String lastVisionStoreDecision = "none";
unsigned long lastVisualStoreMs = 0;
unsigned long visualStoreHistory[64] = {0};
uint8_t visualStoreHistoryIdx = 0;
uint8_t visualStoreHistoryCount = 0;

bool isRemoteMemoryMode(const String& mode) {
  return mode == "remote" || mode == "both";
}

bool isGeminiASR() {
  return asr_provider == "gemini";
}

bool isBackendASR() {
  return asr_provider == "backend";
}

bool asrStartRecording() {
  if (isBackendASR()) {
    return backendAsrChat != nullptr && backendAsrChat->startRecording();
  }
  if (isGeminiASR()) {
    return geminiAsrChat != nullptr && geminiAsrChat->startRecording();
  }
  return asrChat != nullptr && asrChat->startRecording();
}

void asrStopRecording() {
  if (isBackendASR()) {
    if (backendAsrChat != nullptr) backendAsrChat->stopRecording();
    return;
  }
  if (isGeminiASR()) {
    if (geminiAsrChat != nullptr) geminiAsrChat->stopRecording();
    return;
  }
  if (asrChat != nullptr) asrChat->stopRecording();
}

bool asrIsRecording() {
  if (isBackendASR()) {
    return backendAsrChat != nullptr && backendAsrChat->isRecording();
  }
  if (isGeminiASR()) {
    return geminiAsrChat != nullptr && geminiAsrChat->isRecording();
  }
  return asrChat != nullptr && asrChat->isRecording();
}

void asrLoop() {
  if (isBackendASR()) {
    if (backendAsrChat != nullptr) backendAsrChat->loop();
    return;
  }
  if (isGeminiASR()) {
    if (geminiAsrChat != nullptr) geminiAsrChat->loop();
    return;
  }
  if (asrChat != nullptr) asrChat->loop();
}

bool asrHasNewResult() {
  if (isBackendASR()) {
    return backendAsrChat != nullptr && backendAsrChat->hasNewResult();
  }
  if (isGeminiASR()) {
    return geminiAsrChat != nullptr && geminiAsrChat->hasNewResult();
  }
  return asrChat != nullptr && asrChat->hasNewResult();
}

bool asrFinalizeRecording() {
  if (isBackendASR()) {
    return backendAsrChat != nullptr && backendAsrChat->finalizeRecording();
  }
  if (isGeminiASR()) {
    asrStopRecording();
    return false;
  }
  asrStopRecording();
  return false;
}

String asrGetRecognizedText() {
  if (isBackendASR()) {
    if (backendAsrChat != nullptr) return backendAsrChat->getRecognizedText();
    return "";
  }
  if (isGeminiASR()) {
    if (geminiAsrChat != nullptr) return geminiAsrChat->getRecognizedText();
    return "";
  }
  if (asrChat != nullptr) return asrChat->getRecognizedText();
  return "";
}

void asrClearResult() {
  if (isBackendASR()) {
    if (backendAsrChat != nullptr) backendAsrChat->clearResult();
    return;
  }
  if (isGeminiASR()) {
    if (geminiAsrChat != nullptr) geminiAsrChat->clearResult();
    return;
  }
  if (asrChat != nullptr) asrChat->clearResult();
}

int tokenizeUniqueWords(const String& input, String outTokens[], int maxTokens) {
  int count = 0;
  String current = "";
  for (size_t i = 0; i < input.length(); ++i) {
    char ch = input[i];
    if (isalnum((unsigned char)ch)) {
      current += (char)tolower((unsigned char)ch);
    } else if (current.length() > 0) {
      bool exists = false;
      for (int j = 0; j < count; ++j) {
        if (outTokens[j] == current) {
          exists = true;
          break;
        }
      }
      if (!exists && count < maxTokens) {
        outTokens[count++] = current;
      }
      current = "";
    }
  }
  if (current.length() > 0) {
    bool exists = false;
    for (int j = 0; j < count; ++j) {
      if (outTokens[j] == current) {
        exists = true;
        break;
      }
    }
    if (!exists && count < maxTokens) {
      outTokens[count++] = current;
    }
  }
  return count;
}

bool isVisualContextSimilar(const String& previousCtx, const String& newCtx) {
  if (previousCtx.length() == 0 || newCtx.length() == 0) return false;
  if (previousCtx == newCtx) return true;

  const int kMaxTokens = 48;
  String a[kMaxTokens];
  String b[kMaxTokens];
  int countA = tokenizeUniqueWords(previousCtx, a, kMaxTokens);
  int countB = tokenizeUniqueWords(newCtx, b, kMaxTokens);

  if (countA == 0 || countB == 0) return false;

  int overlap = 0;
  for (int i = 0; i < countA; ++i) {
    for (int j = 0; j < countB; ++j) {
      if (a[i] == b[j]) {
        overlap++;
        break;
      }
    }
  }

  int denom = (countA > countB) ? countA : countB;
  int similarityPct = (denom > 0) ? (overlap * 100) / denom : 0;
  return similarityPct >= vision_dedupe_threshold_pct;
}

int countVisualStoresLastHour(unsigned long nowMs) {
  const unsigned long kWindowMs = 3600000UL;
  int count = 0;
  for (uint8_t i = 0; i < visualStoreHistoryCount; ++i) {
    if (nowMs - visualStoreHistory[i] <= kWindowMs) {
      count++;
    }
  }
  return count;
}

void registerVisualStore(unsigned long nowMs) {
  visualStoreHistory[visualStoreHistoryIdx] = nowMs;
  visualStoreHistoryIdx = (visualStoreHistoryIdx + 1) % 64;
  if (visualStoreHistoryCount < 64) visualStoreHistoryCount++;
  lastVisualStoreMs = nowMs;
}

void applyVisualContext(const String& ctx, const char* reasonTag) {
  lastVisionReason = reasonTag;

  if (ctx.length() == 0) {
    lastVisionStoreDecision = "skip:empty_context";
    return;
  }

  String previous = lastVisualContext;
  lastVisualContext = ctx;
  lastVisionRefresh = millis();
  Serial.printf("[Vision] Context updated (%s)\n", reasonTag);

  bool remoteMode = isRemoteMemoryMode(memory_mode);
  if (!memory_store_visual_events || !remoteMode) {
    lastVisionStoreDecision = "skip:memory_disabled_or_local_mode";
    return;
  }

  unsigned long nowMs = millis();
  if (lastVisualStoreMs > 0 && (nowMs - lastVisualStoreMs) < vision_min_store_interval_ms) {
    Serial.println("[Vision] Skip store: min interval guard");
    lastVisionStoreDecision = "skip:min_interval_guard";
    return;
  }

  if (isVisualContextSimilar(previous, ctx)) {
    Serial.println("[Vision] Skip store: similar to previous context");
    lastVisionStoreDecision = "skip:similar_context";
    return;
  }

  int storedLastHour = countVisualStoresLastHour(nowMs);
  if (storedLastHour >= vision_max_events_per_hour) {
    Serial.println("[Vision] Skip store: hourly rate limit reached");
    lastVisionStoreDecision = "skip:hourly_rate_limit";
    return;
  }

  remoteMemory.storeVisualEvent(ctx, reasonTag);
  registerVisualStore(nowMs);
  lastVisionStoreDecision = "stored";
  Serial.printf("[Vision] Stored visual event (%s), count_last_hour=%d\n", reasonTag, storedLastHour + 1);
}

void applyRuntimeModelSwitch(const String& provider, const String& model) {
  if (provider == "backend") {
    if (backendProvider != nullptr) {
      ai_provider = "backend";
      if (model.length() > 0) {
        gemini_model = model;
        backendProvider->setModel(model);
      }
      aiProvider = backendProvider;
      if (visualContextMgr != nullptr) visualContextMgr->setProvider(aiProvider);
      saveConfigToFlash();
      Serial.printf("[Runtime] Provider=backend Model=%s\n", aiProvider->getModel().c_str());
    } else {
      Serial.println("[Runtime] Backend provider not initialized");
    }
    return;
  }

  if (provider == "gemini") {
    if (geminiProvider != nullptr) {
      ai_provider = "gemini";
      gemini_model = model;
      geminiProvider->setModel(model);
      aiProvider = geminiProvider;
      if (visualContextMgr != nullptr) visualContextMgr->setProvider(aiProvider);
      saveConfigToFlash();
      Serial.printf("[Runtime] Provider=gemini Model=%s\n", model.c_str());
    } else {
      Serial.println("[Runtime] Gemini provider not initialized");
    }
    return;
  }

  if (openaiProvider != nullptr) {
    ai_provider = "openai";
    openai_model = model;
    openaiProvider->setModel(model);
    aiProvider = openaiProvider;
    if (visualContextMgr != nullptr) visualContextMgr->setProvider(aiProvider);
    saveConfigToFlash();
    Serial.printf("[Runtime] Provider=openai Model=%s\n", model.c_str());
  } else {
    Serial.println("[Runtime] OpenAI provider not initialized");
  }
}

void processRuntimeCommand() {
  if (!Serial.available()) {
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  // Ignore JSON payloads here. Config JSON is handled by receiveConfig().
  // Without this guard, runtime command parsing can consume provisioning input.
  if (cmd.startsWith("{")) {
    return;
  }

  if (cmd.startsWith("model:")) {
    String model = cmd.substring(6);
    model.trim();
    if (model.length() == 0) {
      Serial.println("[Runtime] model:<slug>");
      return;
    }
    applyRuntimeModelSwitch(ai_provider, model);
    return;
  }

  if (cmd.startsWith("provider:")) {
    String provider = cmd.substring(9);
    provider.trim();
    if (provider != "openai" && provider != "gemini" && provider != "backend") {
      Serial.println("[Runtime] provider must be openai|gemini|backend");
      return;
    }
    String model = (provider == "openai") ? openai_model : gemini_model;
    applyRuntimeModelSwitch(provider, model);
    return;
  }

  if (cmd.startsWith("baseurl:")) {
    String baseUrl = cmd.substring(8);
    baseUrl.trim();
    if (baseUrl.length() == 0) {
      Serial.println("[Runtime] baseurl:<url>");
      return;
    }
    openai_apiBaseUrl = baseUrl;
    if (openaiProvider != nullptr) {
      openaiProvider->setApiConfig(openai_apiKey.c_str(), openai_apiBaseUrl.c_str());
    }
    saveConfigToFlash();
    Serial.printf("[Runtime] openai_apiBaseUrl=%s\n", openai_apiBaseUrl.c_str());
    return;
  }

  if (cmd.startsWith("ttsbaseurl:")) {
    String baseUrl = cmd.substring(11);
    baseUrl.trim();
    if (baseUrl.length() == 0) {
      Serial.println("[Runtime] ttsbaseurl:<url>");
      return;
    }
    tts_apiBaseUrl = baseUrl;
    if (ttsProvider != nullptr) {
      ttsProvider->setApiConfig(tts_apiKey.c_str(), tts_apiBaseUrl.c_str());
    }
    saveConfigToFlash();
    Serial.printf("[Runtime] tts_apiBaseUrl=%s\n", tts_apiBaseUrl.c_str());
    return;
  }

  if (cmd.startsWith("ttsmodel:")) {
    String model = cmd.substring(9);
    model.trim();
    if (model.length() == 0) {
      Serial.println("[Runtime] ttsmodel:<model>");
      return;
    }
    tts_openai_model = model;
    if (ttsProvider != nullptr) {
      ttsProvider->client().setTTSConfig(tts_openai_model.c_str(), tts_openai_voice.c_str(), tts_openai_speed.c_str());
    }
    saveConfigToFlash();
    Serial.printf("[Runtime] tts_model=%s\n", tts_openai_model.c_str());
    return;
  }

  if (cmd == "status") {
    Serial.printf("[Runtime] asr_provider=%s asr_api_url=%s interaction=%s manual_record=%s provider=%s model=%s tts_engine=%s tts_base=%s tts_model=%s backend_api_url=%s\n",
                  asr_provider.c_str(),
                  asr_api_url.c_str(),
                  single_turn_mode ? "single_turn" : "continuous",
                  manual_record_control ? "true" : "false",
                  ai_provider.c_str(),
                  aiProvider != nullptr ? aiProvider->getModel().c_str() : "",
                  use_backend_tts ? "backend" : (use_elevenlabs_tts ? "elevenlabs" : "openai_compatible"),
                  tts_apiBaseUrl.c_str(),
                  tts_openai_model.c_str(),
                  backend_api_url.c_str());
    return;
  }

  if (cmd == "diag") {
    Serial.printf("[Diag] state=%d initialized=%s continuous=%s\n",
                  (int)currentState,
                  systemInitialized ? "true" : "false",
                  continuousMode ? "true" : "false");
    Serial.printf("[Diag] asr_provider=%s\n", asr_provider.c_str());
    Serial.printf("[Diag] single_turn_mode=%s\n", single_turn_mode ? "true" : "false");
    Serial.printf("[Diag] manual_record_control=%s\n", manual_record_control ? "true" : "false");
    Serial.printf("[Diag] wifi=%s rssi=%d ip=%s\n",
                  WiFi.status() == WL_CONNECTED ? "connected" : "disconnected",
                  WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());
    Serial.printf("[Diag] free_heap=%u\n", ESP.getFreeHeap());
    Serial.printf("[Diag] i2s_out pins BCLK=%d LRC=%d DOUT=%d\n", I2S_BCLK, I2S_LRC, I2S_DOUT);
    Serial.printf("[Diag] i2s_mic pins SCK=%d WS=%d SD=%d\n",
                  I2S_MIC_SERIAL_CLOCK, I2S_MIC_LEFT_RIGHT_CLOCK, I2S_MIC_SERIAL_DATA);
    Serial.printf("[Diag] audio_running=%s tts_ws_playing=%s\n",
                  audio.isRunning() ? "true" : "false",
                  (subscription == "pro" && ttsChat != nullptr && ttsChat->isPlaying()) ? "true" : "false");
    Serial.printf("[Diag] tts_engine=%s eleven_voice=%s eleven_model=%s backend_api_url=%s\n",
                  use_backend_tts ? "backend" : (use_elevenlabs_tts ? "elevenlabs" : "openai_compatible"),
                  elevenlabs_voice_id.c_str(),
                  elevenlabs_model_id.c_str(),
                  backend_api_url.c_str());
    Serial.printf("[Diag] vision refresh=%lums on_start=%s on_turn=%s dedupe=%d%% min_store=%lums max_hour=%d\n",
                  vision_refresh_interval,
                  vision_on_conversation_start ? "true" : "false",
                  vision_capture_on_user_turn ? "true" : "false",
                  vision_dedupe_threshold_pct,
                  vision_min_store_interval_ms,
                  vision_max_events_per_hour);
    return;
  }

  if (cmd.startsWith("testtts:")) {
    String text = cmd.substring(8);
    text.trim();
    if (text.length() == 0) {
      text = "Speaker test from chat configurable.";
    }
    if (subscription == "pro") {
      if (ttsChat == nullptr) {
        Serial.println("[Runtime] testtts failed: ttsChat not initialized");
        return;
      }
      ttsCompleted = false;
      bool ok = ttsChat->speak(text.c_str());
      Serial.printf("[Runtime] testtts pro=%s\n", ok ? "ok" : "failed");
    } else {
      bool ok = false;
      if (use_backend_tts && backendTTS.isConfigured()) {
        ok = backendTTS.speak(text);
      } else if (use_elevenlabs_tts) {
        ok = elevenlabsTTS.speak(text);
      } else if (ttsProvider != nullptr) {
        ok = ttsProvider->client().textToSpeech(text);
      }
      Serial.printf("[Runtime] testtts free=%s\n", ok ? "ok" : "failed");
    }
    return;
  }

  if (cmd == "start") {
    if (!systemInitialized) {
      Serial.println("[Runtime] System not initialized yet");
      return;
    }
    if (!continuousMode) {
      startContinuousMode();
    } else {
      Serial.println("[Runtime] Already in continuous mode");
    }
    return;
  }

  if (cmd == "stop") {
    if (manual_record_control &&
        continuousMode &&
        currentState == STATE_LISTENING &&
        asrIsRecording()) {
      bool ok = asrFinalizeRecording();
      Serial.printf("[Runtime] Manual stop finalize=%s\n", ok ? "ok" : "failed");
      return;
    }
    if (continuousMode) {
      stopContinuousMode();
    } else {
      Serial.println("[Runtime] Already stopped");
    }
    return;
  }
}

String buildContextAwarePrompt(const String& basePrompt, const String& visualContext) {
  return OpenAIVisionProxy::BuildContextPrompt(basePrompt, visualContext);
}

bool captureJpegStub(uint8_t** outData, size_t* outSize) {
  (void)outData;
  (void)outSize;
  return false;
}

void releaseJpegStub(uint8_t* data) {
  if (data != nullptr) {
    free(data);
  }
}

// ============================================================================
// Flash Storage Functions
// ============================================================================

/**
 * Save configuration to Flash
 */
bool saveConfigToFlash() {
  preferences.begin("voice_config", false);
  
  preferences.putString("wifi_ssid", wifi_ssid);
  preferences.putString("wifi_pass", wifi_password);
  preferences.putString("subscription", subscription);
  preferences.putString("asr_provider", asr_provider);
  preferences.putString("asr_key", asr_api_key);
  preferences.putString("asr_cluster", asr_cluster);
  preferences.putString("asr_api_url", asr_api_url);
  preferences.putString("openai_key", openai_apiKey);
  preferences.putString("openai_url", openai_apiBaseUrl);
  preferences.putString("openai_model", openai_model);
  preferences.putString("tts_key", tts_apiKey);
  preferences.putString("tts_url", tts_apiBaseUrl);
  preferences.putString("tts_openai_model", tts_openai_model);
  preferences.putString("tts_openai_voice", tts_openai_voice);
  preferences.putString("tts_openai_speed", tts_openai_speed);
  preferences.putBool("use_elevenlabs_tts", use_elevenlabs_tts);
  preferences.putString("elevenlabs_api_key", elevenlabs_api_key);
  preferences.putString("elevenlabs_voice_id", elevenlabs_voice_id);
  preferences.putString("elevenlabs_model_id", elevenlabs_model_id);
  preferences.putString("elevenlabs_output_format", elevenlabs_output_format);
  preferences.putString("ai_provider", ai_provider);
  preferences.putString("backend_api_url", backend_api_url);
  preferences.putString("backend_api_key", backend_api_key);
  preferences.putBool("use_backend_tts", use_backend_tts);
  preferences.putString("gemini_key", gemini_apiKey);
  preferences.putString("gemini_model", gemini_model);
  preferences.putString("sys_prompt", system_prompt);
  preferences.putBool("vision_enabled", vision_enabled);
  preferences.putULong("vision_refresh", vision_refresh_interval);
  preferences.putBool("vision_on_start", vision_on_conversation_start);
  preferences.putBool("vision_on_turn", vision_capture_on_user_turn);
  preferences.putString("vision_prompt", vision_prompt);
  preferences.putInt("vision_dedupe", vision_dedupe_threshold_pct);
  preferences.putULong("vision_min_store", vision_min_store_interval_ms);
  preferences.putInt("vision_max_hour", vision_max_events_per_hour);
  preferences.putString("memory_api_url", memory_api_url);
  preferences.putString("memory_api_key", memory_api_key);
  preferences.putString("device_id", device_id);
  preferences.putString("memory_mode", memory_mode);
  preferences.putBool("mem_visual", memory_store_visual_events);
  preferences.putBool("single_turn_mode", single_turn_mode);
  preferences.putBool("manual_record_control", manual_record_control);
  preferences.putBool("web_enabled", web_control_enabled);
  preferences.putInt("web_port", web_port);
  preferences.putInt("web_ws_port", web_ws_port);
  
  if (subscription == "pro") {
    preferences.putString("minimax_key", minimax_apiKey);
    preferences.putString("minimax_gid", minimax_groupId);
    preferences.putString("tts_voice", tts_voice_id);
    preferences.putFloat("tts_speed", tts_speed);
    preferences.putFloat("tts_volume", tts_volume);
    preferences.putString("tts_model", tts_model);
    preferences.putString("tts_format", tts_audio_format);
    preferences.putInt("tts_sample", tts_sample_rate);
    preferences.putInt("tts_bitrate", tts_bitrate);
  }
  
  preferences.putBool("configured", true);
  preferences.end();
  
  Serial.println("[Flash] Config saved to Flash");
  return true;
}

/**
 * Load configuration from Flash
 */
bool loadConfigFromFlash() {
  preferences.begin("voice_config", true);
  
  if (!preferences.getBool("configured", false)) {
    preferences.end();
    Serial.println("[Flash] No config found in Flash");
    return false;
  }
  
  wifi_ssid = preferences.getString("wifi_ssid", "");
  wifi_password = preferences.getString("wifi_pass", "");
  subscription = preferences.getString("subscription", "free");
  asr_provider = preferences.getString("asr_provider", "bytedance");
  asr_api_key = preferences.getString("asr_key", "");
  asr_cluster = preferences.getString("asr_cluster", "volcengine_input_en");
  asr_api_url = preferences.getString("asr_api_url", "");
  openai_apiKey = preferences.getString("openai_key", "");
  openai_apiBaseUrl = preferences.getString("openai_url", "");
  openai_model = preferences.getString("openai_model", "gpt-4.1-nano");
  tts_apiKey = preferences.getString("tts_key", "");
  tts_apiBaseUrl = preferences.getString("tts_url", "https://api.openai.com");
  tts_openai_model = preferences.getString("tts_openai_model", "gpt-4o-mini-tts");
  tts_openai_voice = preferences.getString("tts_openai_voice", "alloy");
  tts_openai_speed = preferences.getString("tts_openai_speed", "1.0");
  use_elevenlabs_tts = preferences.getBool("use_elevenlabs_tts", true);
  elevenlabs_api_key = preferences.getString("elevenlabs_api_key", "");
  elevenlabs_voice_id = preferences.getString("elevenlabs_voice_id", "EST9Ui6982FZPSi7gCHi");
  elevenlabs_model_id = preferences.getString("elevenlabs_model_id", "eleven_flash_v2_5");
  elevenlabs_output_format = preferences.getString("elevenlabs_output_format", "mp3_22050_32");
  ai_provider = preferences.getString("ai_provider", "openai");
  backend_api_url = preferences.getString("backend_api_url", "");
  backend_api_key = preferences.getString("backend_api_key", "");
  use_backend_tts = preferences.getBool("use_backend_tts", true);
  gemini_apiKey = preferences.getString("gemini_key", "");
  gemini_model = preferences.getString("gemini_model", "gemini-2.0-flash");
  system_prompt = preferences.getString("sys_prompt", "You are a helpful AI assistant.");
  vision_enabled = preferences.getBool("vision_enabled", false);
  vision_refresh_interval = preferences.getULong("vision_refresh", 60000);
  vision_on_conversation_start = preferences.getBool("vision_on_start", true);
  vision_capture_on_user_turn = preferences.getBool("vision_on_turn", true);
  vision_prompt = preferences.getString("vision_prompt", "Briefly describe what you see");
  vision_dedupe_threshold_pct = preferences.getInt("vision_dedupe", 90);
  vision_min_store_interval_ms = preferences.getULong("vision_min_store", 30000);
  vision_max_events_per_hour = preferences.getInt("vision_max_hour", 30);
  memory_api_url = preferences.getString("memory_api_url", "");
  memory_api_key = preferences.getString("memory_api_key", "");
  device_id = preferences.getString("device_id", "");
  memory_mode = preferences.getString("memory_mode", "local");
  memory_store_visual_events = preferences.getBool("mem_visual", true);
  single_turn_mode = preferences.getBool("single_turn_mode", true);
  manual_record_control = preferences.getBool("manual_record_control", true);
  web_control_enabled = preferences.getBool("web_enabled", false);
  web_port = preferences.getInt("web_port", 80);
  web_ws_port = preferences.getInt("web_ws_port", 81);
  
  if (subscription == "pro") {
    minimax_apiKey = preferences.getString("minimax_key", "");
    minimax_groupId = preferences.getString("minimax_gid", "");
    tts_voice_id = preferences.getString("tts_voice", "female-tianmei");
    tts_speed = preferences.getFloat("tts_speed", 1.0);
    tts_volume = preferences.getFloat("tts_volume", 1.0);
    tts_model = preferences.getString("tts_model", "speech-2.6-hd");
    tts_audio_format = preferences.getString("tts_format", "mp3");
    tts_sample_rate = preferences.getInt("tts_sample", 32000);
    tts_bitrate = preferences.getInt("tts_bitrate", 128000);
  }
  
  preferences.end();
  
  Serial.println("[Flash] Config loaded from Flash");
  Serial.println("Subscription: " + subscription);
  Serial.println("WiFi SSID: " + wifi_ssid);
  
  return true;
}

/**
 * Clear configuration in Flash
 */
void clearFlashConfig() {
  preferences.begin("voice_config", false);
  preferences.clear();
  preferences.end();
  Serial.println("[Flash] Flash config cleared");
}

// ============================================================================
// Configuration Reception Functions
// ============================================================================

/**
 * Receive JSON configuration from serial port
 * Uses buffering to handle fragmented serial data
 */
bool receiveConfig() {
  static String jsonBuffer = "";
  static unsigned long lastReceiveTime = 0;
  static bool receiving = false;
  
  // Check if we should start looking for data
  if (!receiving && Serial.available() > 0) {
    // Peek at first character to see if it's a '{'
    char first = Serial.peek();
    if (first != '{') {
      // Clear any junk before the JSON starts
      while (Serial.available() > 0 && Serial.peek() != '{') {
        Serial.read();
      }
    }
  }
  
  // Read all available data
  while (Serial.available() > 0) {
    char c = Serial.read();
    
    // Start receiving when we see opening brace
    if (c == '{' && !receiving) {
      jsonBuffer = "{";
      receiving = true;
      lastReceiveTime = millis();
      continue;
    }
    
    // Only accumulate if we're receiving
    if (receiving) {
      jsonBuffer += c;
      lastReceiveTime = millis();
      
      // If we receive closing brace, try to parse
      if (c == '}') {
        // Wait longer for any remaining data in buffer
        delay(200);
        
        // Read any remaining data until newline or no more data
        int noDataCount = 0;
        while (noDataCount < 10) {  // Wait for up to 10 cycles of no data
          if (Serial.available() > 0) {
            char extra = Serial.read();
            if (extra == '\n' || extra == '\r') {
              break; // Stop at newline
            }
            jsonBuffer += extra;
            noDataCount = 0;  // Reset counter when data received
            delay(2);
          } else {
            delay(5);
            noDataCount++;
          }
        }
        
        jsonBuffer.trim();
        
        // Check if we have a complete JSON
        if (jsonBuffer.startsWith("{") && jsonBuffer.endsWith("}")) {
          // Parse JSON with larger buffer
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, jsonBuffer);
          
          if (error) {
            Serial.print("[Error] JSON parse failed: ");
            Serial.println(error.c_str());
            jsonBuffer = "";
            receiving = false;
            return false;
          }
          
          // Clear buffer
          jsonBuffer = "";
          receiving = false;
          
          // Extract configuration
          if (doc.containsKey("wifi_ssid")) {
            wifi_ssid = doc["wifi_ssid"].as<String>();
          }
          if (doc.containsKey("wifi_password")) {
            wifi_password = doc["wifi_password"].as<String>();
          }
          if (doc.containsKey("subscription")) {
            subscription = doc["subscription"].as<String>();
          }
          if (doc.containsKey("asr_api_key")) {
            asr_api_key = doc["asr_api_key"].as<String>();
          }
          if (doc.containsKey("asr_provider")) {
            asr_provider = doc["asr_provider"].as<String>();
          }
          if (doc.containsKey("asr_cluster")) {
            asr_cluster = doc["asr_cluster"].as<String>();
          }
          if (doc.containsKey("asr_api_url")) {
            asr_api_url = doc["asr_api_url"].as<String>();
          }
          if (doc.containsKey("openai_apiKey")) {
            openai_apiKey = doc["openai_apiKey"].as<String>();
          }
          if (doc.containsKey("openai_apiBaseUrl")) {
            openai_apiBaseUrl = doc["openai_apiBaseUrl"].as<String>();
          }
          if (doc.containsKey("openai_model")) {
            openai_model = doc["openai_model"].as<String>();
          }
          if (doc.containsKey("tts_apiKey")) {
            tts_apiKey = doc["tts_apiKey"].as<String>();
          }
          if (doc.containsKey("tts_apiBaseUrl")) {
            tts_apiBaseUrl = doc["tts_apiBaseUrl"].as<String>();
          }
          if (doc.containsKey("tts_openai_model")) {
            tts_openai_model = doc["tts_openai_model"].as<String>();
          }
          if (doc.containsKey("tts_openai_voice")) {
            tts_openai_voice = doc["tts_openai_voice"].as<String>();
          }
          if (doc.containsKey("tts_openai_speed")) {
            tts_openai_speed = doc["tts_openai_speed"].as<String>();
          }
          if (doc.containsKey("use_elevenlabs_tts")) {
            use_elevenlabs_tts = doc["use_elevenlabs_tts"].as<bool>();
          }
          if (doc.containsKey("elevenlabs_api_key")) {
            elevenlabs_api_key = doc["elevenlabs_api_key"].as<String>();
          }
          if (doc.containsKey("elevenlabs_voice_id")) {
            elevenlabs_voice_id = doc["elevenlabs_voice_id"].as<String>();
          }
          if (doc.containsKey("elevenlabs_model_id")) {
            elevenlabs_model_id = doc["elevenlabs_model_id"].as<String>();
          }
          if (doc.containsKey("elevenlabs_output_format")) {
            elevenlabs_output_format = doc["elevenlabs_output_format"].as<String>();
          }
          if (doc.containsKey("ai_provider")) {
            ai_provider = doc["ai_provider"].as<String>();
          }
          if (doc.containsKey("backend_api_url")) {
            backend_api_url = doc["backend_api_url"].as<String>();
          }
          if (doc.containsKey("backend_api_key")) {
            backend_api_key = doc["backend_api_key"].as<String>();
          }
          if (doc.containsKey("use_backend_tts")) {
            use_backend_tts = doc["use_backend_tts"].as<bool>();
          }
          if (doc.containsKey("gemini_apiKey")) {
            gemini_apiKey = doc["gemini_apiKey"].as<String>();
          }
          if (doc.containsKey("gemini_model")) {
            gemini_model = doc["gemini_model"].as<String>();
          }
          if (doc.containsKey("system_prompt")) {
            system_prompt = doc["system_prompt"].as<String>();
          }
          if (doc.containsKey("vision_enabled")) {
            vision_enabled = doc["vision_enabled"].as<bool>();
          }
          if (doc.containsKey("vision_refresh_interval")) {
            vision_refresh_interval = doc["vision_refresh_interval"].as<unsigned long>();
          }
          if (doc.containsKey("vision_on_conversation_start")) {
            vision_on_conversation_start = doc["vision_on_conversation_start"].as<bool>();
          }
          if (doc.containsKey("vision_capture_on_user_turn")) {
            vision_capture_on_user_turn = doc["vision_capture_on_user_turn"].as<bool>();
          }
          if (doc.containsKey("vision_prompt")) {
            vision_prompt = doc["vision_prompt"].as<String>();
          }
          if (doc.containsKey("vision_dedupe_threshold_pct")) {
            vision_dedupe_threshold_pct = doc["vision_dedupe_threshold_pct"].as<int>();
          }
          if (doc.containsKey("vision_min_store_interval_ms")) {
            vision_min_store_interval_ms = doc["vision_min_store_interval_ms"].as<unsigned long>();
          }
          if (doc.containsKey("vision_max_events_per_hour")) {
            vision_max_events_per_hour = doc["vision_max_events_per_hour"].as<int>();
          }
          if (doc.containsKey("memory_api_url")) {
            memory_api_url = doc["memory_api_url"].as<String>();
          }
          if (doc.containsKey("memory_api_key")) {
            memory_api_key = doc["memory_api_key"].as<String>();
          }
          if (doc.containsKey("device_id")) {
            device_id = doc["device_id"].as<String>();
          }
          if (doc.containsKey("memory_mode")) {
            memory_mode = doc["memory_mode"].as<String>();
          }
          if (doc.containsKey("memory_store_visual_events")) {
            memory_store_visual_events = doc["memory_store_visual_events"].as<bool>();
          }
          if (doc.containsKey("single_turn_mode")) {
            single_turn_mode = doc["single_turn_mode"].as<bool>();
          }
          if (doc.containsKey("manual_record_control")) {
            manual_record_control = doc["manual_record_control"].as<bool>();
          }
          if (doc.containsKey("web_control_enabled")) {
            web_control_enabled = doc["web_control_enabled"].as<bool>();
          }
          if (doc.containsKey("web_port")) {
            web_port = doc["web_port"].as<int>();
          }
          if (doc.containsKey("web_ws_port")) {
            web_ws_port = doc["web_ws_port"].as<int>();
          }
          
          // Pro version additional configuration
          if (subscription == "pro") {
            if (doc.containsKey("minimax_apiKey")) {
              minimax_apiKey = doc["minimax_apiKey"].as<String>();
            }
            if (doc.containsKey("minimax_groupId")) {
              minimax_groupId = doc["minimax_groupId"].as<String>();
            }
            if (doc.containsKey("tts_voice_id")) {
              tts_voice_id = doc["tts_voice_id"].as<String>();
            }
            if (doc.containsKey("tts_speed")) {
              tts_speed = doc["tts_speed"].as<float>();
            }
            if (doc.containsKey("tts_volume")) {
              tts_volume = doc["tts_volume"].as<float>();
            }
            if (doc.containsKey("tts_model")) {
              tts_model = doc["tts_model"].as<String>();
            }
            if (doc.containsKey("tts_audio_format")) {
              tts_audio_format = doc["tts_audio_format"].as<String>();
            }
            if (doc.containsKey("tts_sample_rate")) {
              tts_sample_rate = doc["tts_sample_rate"].as<int>();
            }
            if (doc.containsKey("tts_bitrate")) {
              tts_bitrate = doc["tts_bitrate"].as<int>();
            }
          }
          
          // Validate required configuration
          if (asr_provider != "bytedance" && asr_provider != "gemini" && asr_provider != "backend") {
            Serial.println("Error: asr_provider must be bytedance|gemini|backend");
            return false;
          }
          if (ai_provider != "openai" && ai_provider != "gemini" && ai_provider != "backend") {
            Serial.println("Error: ai_provider must be openai|gemini|backend");
            return false;
          }

          bool useGeminiASR = (asr_provider == "gemini");
          bool useBackendASR = (asr_provider == "backend");
          bool needOpenAI = (ai_provider == "openai");

          if (backend_api_url.length() == 0) {
            if (memory_api_url.length() > 0) backend_api_url = memory_api_url;
            else if (asr_api_url.length() > 0) backend_api_url = asr_api_url;
          }
          if (backend_api_key.length() == 0) {
            if (memory_api_key.length() > 0) backend_api_key = memory_api_key;
            else if (asr_api_key.length() > 0) backend_api_key = asr_api_key;
          }

          if (wifi_ssid.length() == 0 || wifi_password.length() == 0 ||
              (!useGeminiASR && !useBackendASR && asr_api_key.length() == 0) || (needOpenAI && openai_apiKey.length() == 0)) {
            Serial.println("Error: Missing required config");
            if (wifi_ssid.length() == 0) Serial.println("  - wifi_ssid is empty");
            if (wifi_password.length() == 0) Serial.println("  - wifi_password is empty");
            if (!useGeminiASR && !useBackendASR && asr_api_key.length() == 0) Serial.println("  - asr_api_key is empty");
            if (needOpenAI && openai_apiKey.length() == 0) Serial.println("  - openai_apiKey is empty");
            return false;
          }

          if (subscription == "free" && !use_elevenlabs_tts && tts_apiKey.length() == 0) {
            // Default to chat key for backward compatibility.
            tts_apiKey = openai_apiKey;
          }

          if (subscription == "free" && use_elevenlabs_tts && elevenlabs_api_key.length() == 0) {
            Serial.println("Error: ElevenLabs TTS enabled but elevenlabs_api_key missing");
            return false;
          }

          if (ai_provider == "gemini" && gemini_apiKey.length() == 0) {
            Serial.println("Error: Gemini provider selected but gemini_apiKey missing");
            return false;
          }

          if (ai_provider == "backend") {
            if (backend_api_url.length() == 0) {
              Serial.println("Error: Backend provider selected but backend_api_url missing");
              return false;
            }
            if (backend_api_key.length() == 0) {
              Serial.println("Error: Backend provider selected but backend_api_key missing");
              return false;
            }
          }

          if (useGeminiASR && gemini_apiKey.length() == 0) {
            Serial.println("Error: Gemini ASR selected but gemini_apiKey missing");
            return false;
          }

          if (useBackendASR) {
            if (asr_api_url.length() == 0 && memory_api_url.length() > 0) {
              asr_api_url = memory_api_url;
            }
            if (asr_api_key.length() == 0 && memory_api_key.length() > 0) {
              asr_api_key = memory_api_key;
            }
            if (asr_api_url.length() == 0) {
              Serial.println("Error: Backend ASR selected but asr_api_url missing");
              return false;
            }
          }
          
          if (subscription == "pro" && (minimax_apiKey.length() == 0 || minimax_groupId.length() == 0)) {
            Serial.println("Error: Pro version missing MiniMax config");
            return false;
          }
          
          Serial.println("\n[Config] Configuration received successfully!");
          Serial.println("[Config] Mode: " + subscription);
          
          return true;
        }
      }
    }
  }
  
  // Timeout check - if no data received for 3 seconds, clear buffer
  if (receiving && jsonBuffer.length() > 0 && (millis() - lastReceiveTime > 3000)) {
    Serial.println("[Warning] Config receive timeout, buffer cleared");
    jsonBuffer = "";
    receiving = false;
  }
  
  return false;
}

// ============================================================================
// System Initialization Function
// ============================================================================

bool initializeSystem() {
  Serial.println("\n----- System Initialization -----");
  
  // ========== WiFi Connection ==========
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());
  Serial.println("Connecting to WiFi...");
  
  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    Serial.print('.');
    delay(1000);
    attempt++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed!");
    return false;
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Free Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");

  // ========== ASR Initialization ==========
  if (isBackendASR()) {
    if (asr_api_url.length() == 0 && memory_api_url.length() > 0) {
      asr_api_url = memory_api_url;
    }
    if (asr_api_key.length() == 0 && memory_api_key.length() > 0) {
      asr_api_key = memory_api_key;
    }
    backendAsrChat = new BackendASRChat(asr_api_url.c_str(), asr_api_key.c_str());
    if (!backendAsrChat->initINMP441Microphone(I2S_MIC_SERIAL_CLOCK, I2S_MIC_LEFT_RIGHT_CLOCK, I2S_MIC_SERIAL_DATA)) {
      Serial.println("Microphone init failed!");
      return false;
    }
    backendAsrChat->setAudioParams(SAMPLE_RATE, 16, 1);
    backendAsrChat->setSilenceDuration(1400);
    backendAsrChat->setMaxRecordingSeconds(6);
    backendAsrChat->setManualStopOnly(manual_record_control);
    backendAsrChat->setTimeoutNoSpeechCallback([]() {
      if (continuousMode) {
        stopContinuousMode();
      }
    });
    Serial.printf("ASR initialized: Backend (%s)\n", asr_api_url.c_str());
  } else if (isGeminiASR()) {
    geminiAsrChat = new GeminiASRChat(gemini_apiKey.c_str(), gemini_model.c_str());
    if (!geminiAsrChat->initINMP441Microphone(I2S_MIC_SERIAL_CLOCK, I2S_MIC_LEFT_RIGHT_CLOCK, I2S_MIC_SERIAL_DATA)) {
      Serial.println("Microphone init failed!");
      return false;
    }
    geminiAsrChat->setAudioParams(SAMPLE_RATE, 16, 1);
    geminiAsrChat->setSilenceDuration(900);
    geminiAsrChat->setMaxRecordingSeconds(6);
    geminiAsrChat->setTimeoutNoSpeechCallback([]() {
      if (continuousMode) {
        stopContinuousMode();
      }
    });
    Serial.printf("ASR initialized: Gemini (%s)\n", gemini_model.c_str());
  } else {
    asrChat = new ArduinoASRChat(asr_api_key.c_str(), asr_cluster.c_str());

    if (!asrChat->initINMP441Microphone(I2S_MIC_SERIAL_CLOCK, I2S_MIC_LEFT_RIGHT_CLOCK, I2S_MIC_SERIAL_DATA)) {
      Serial.println("Microphone init failed!");
      return false;
    }

    asrChat->setAudioParams(SAMPLE_RATE, 16, 1);
    asrChat->setSilenceDuration(1000);
    asrChat->setMaxRecordingSeconds(50);

    asrChat->setTimeoutNoSpeechCallback([]() {
      if (continuousMode) {
        stopContinuousMode();
      }
    });

    if (!asrChat->connectWebSocket()) {
      Serial.println("ASR service connection failed!");
      return false;
    }
    Serial.printf("ASR initialized: ByteDance (%s)\n", asr_cluster.c_str());
  }
  
  // ========== LLM Provider Initialization ==========
  if (openai_apiKey.length() > 0) {
    openaiProvider = new OpenAIProvider(openai_apiKey.c_str(), openai_apiBaseUrl.c_str());
    openaiProvider->setModel(openai_model);
    openaiProvider->setSystemPrompt(system_prompt);
    openaiProvider->enableMemory(true);
  }

  // Separate provider for TTS credentials/base URL (defaults to OpenAI if not provided)
  if (tts_apiKey.length() == 0) {
    tts_apiKey = openai_apiKey;
  }
  if (tts_apiBaseUrl.length() == 0) {
    tts_apiBaseUrl = "https://api.openai.com";
  }
  if (tts_apiKey.length() > 0) {
    ttsProvider = new OpenAIProvider(tts_apiKey.c_str(), tts_apiBaseUrl.c_str());
    ttsProvider->client().setTTSConfig(tts_openai_model.c_str(), tts_openai_voice.c_str(), tts_openai_speed.c_str());
  }

  if (backend_api_url.length() == 0) {
    if (memory_api_url.length() > 0) backend_api_url = memory_api_url;
    else if (asr_api_url.length() > 0) backend_api_url = asr_api_url;
  }
  if (backend_api_key.length() == 0) {
    if (memory_api_key.length() > 0) backend_api_key = memory_api_key;
    else if (asr_api_key.length() > 0) backend_api_key = asr_api_key;
  }
  if (backend_api_url.length() > 0 && backend_api_key.length() > 0) {
    backendProvider = new BackendLLMProvider(backend_api_url.c_str(), backend_api_key.c_str());
    backendProvider->setModel(gemini_model);
    backendProvider->setSystemPrompt(system_prompt);
    backendProvider->enableMemory(true);
  }

  if (ai_provider == "backend") {
    if (backendProvider == nullptr) {
      Serial.println("Backend provider selected but backend_api_url/backend_api_key missing");
      return false;
    }
    aiProvider = backendProvider;
    Serial.printf("LLM Provider: Backend (%s)\n", backendProvider->getModel().c_str());
  } else if (ai_provider == "gemini") {
    geminiProvider = new GeminiProvider(gemini_apiKey.c_str());
    geminiProvider->setModel(gemini_model);
    geminiProvider->setSystemPrompt(system_prompt);
    geminiProvider->enableMemory(true);
    aiProvider = geminiProvider;
    Serial.printf("LLM Provider: Gemini (%s)\n", gemini_model.c_str());
  } else {
    if (openaiProvider == nullptr) {
      Serial.println("OpenAI provider selected but openai_apiKey missing");
      return false;
    }
    aiProvider = openaiProvider;
    Serial.printf("LLM Provider: OpenAI (%s)\n", openai_model.c_str());
  }

  visualContextMgr = new VisualContextManager(aiProvider);
  visualContextMgr->setCaptureCallbacks(captureJpegStub, releaseJpegStub);
  visualContextMgr->setPrompt(vision_prompt);
  if (vision_enabled) {
    Serial.println("[Vision] Enabled, but using capture stub. Attach camera callback for live capture.");
  }

  // ========== TTS Initialization (Based on subscription type) ==========
  if (subscription == "pro") {
    // Pro version: Use MiniMax WebSocket TTS (lower latency)
    ttsChat = new ArduinoTTSChat(minimax_apiKey.c_str());

    // Configure TTS parameters (MUST be done BEFORE speaker init)
    ttsChat->setVoiceId(tts_voice_id.c_str());
    ttsChat->setSpeed(tts_speed);
    ttsChat->setVolume(tts_volume);
    ttsChat->setAudioParams(tts_sample_rate, tts_bitrate);

    // Initialize I2S speaker for WebSocket TTS
    Serial.println("Initializing WebSocket TTS speaker...");
    if (!ttsChat->initMAX98357Speaker(I2S_BCLK, I2S_LRC, I2S_DOUT)) {
      Serial.println("WebSocket TTS speaker initialization failed!");
      return false;
    }

    // Set callbacks
    ttsChat->setCompletionCallback(onTTSComplete);
    ttsChat->setErrorCallback(onTTSError);

    // Connect to MiniMax WebSocket
    Serial.println("Connecting to MiniMax TTS WebSocket...");
    if (!ttsChat->connectWebSocket()) {
      Serial.println("WebSocket connection failed!");
      return false;
    }

    Serial.println("TTS Mode: MiniMax WebSocket (Pro)");
    Serial.printf("Config: Voice=%s, Speed=%.1f, SampleRate=%d\n",
                  tts_voice_id.c_str(), tts_speed, tts_sample_rate);
  } else {
    // Free version: Use ElevenLabs by default, fallback to OpenAI-compatible TTS.
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    audio.setVolume(20);
    backendTTS.setConfig(
      backend_api_url.c_str(),
      backend_api_key.c_str(),
      elevenlabs_voice_id.c_str(),
      elevenlabs_model_id.c_str(),
      elevenlabs_output_format.c_str()
    );

    if (use_backend_tts && backendTTS.isConfigured()) {
      Serial.printf("TTS Mode: Backend proxy (%s)\n", backend_api_url.c_str());
    } else if (use_elevenlabs_tts) {
      elevenlabsTTS.setConfig(
        elevenlabs_api_key.c_str(),
        elevenlabs_voice_id.c_str(),
        elevenlabs_model_id.c_str(),
        elevenlabs_output_format.c_str()
      );
      Serial.printf("TTS Mode: ElevenLabs (voice=%s, model=%s)\n",
                    elevenlabs_voice_id.c_str(), elevenlabs_model_id.c_str());
    } else {
      Serial.printf("TTS Mode: OpenAI-compatible (%s, model=%s)\n", tts_apiBaseUrl.c_str(), tts_openai_model.c_str());
    }
  }

  // ========== Remote Memory ==========
  if (device_id.length() == 0) {
    device_id = WiFi.macAddress();
  }
  remoteMemory.setConfig(memory_api_url.c_str(), memory_api_key.c_str(), device_id.c_str());
  bool remoteEnabled = isRemoteMemoryMode(memory_mode) &&
                       memory_api_url.length() > 0 && memory_api_key.length() > 0;
  remoteMemory.setEnabled(remoteEnabled);
  Serial.printf("Memory mode: %s (Remote API %s)\n", memory_mode.c_str(), remoteEnabled ? "enabled" : "disabled");

  // ========== Web Control ==========
  if (web_control_enabled) {
    webControl = new WebControl((uint16_t)web_port, (uint16_t)web_ws_port);
    webControl->setStatusHandler([]() -> String {
      DynamicJsonDocument doc(512);
      doc["state"] = (int)currentState;
      doc["continuous_mode"] = continuousMode;
      doc["provider"] = aiProvider != nullptr ? aiProvider->getProviderName() : "";
      doc["model"] = aiProvider != nullptr ? aiProvider->getModel() : "";
      doc["wifi_rssi"] = WiFi.RSSI();
      doc["heap"] = ESP.getFreeHeap();
      doc["vision_last_reason"] = lastVisionReason;
      doc["vision_last_store_decision"] = lastVisionStoreDecision;
      doc["vision_last_refresh_ms_ago"] = lastVisionRefresh > 0 ? (millis() - lastVisionRefresh) : -1;
      doc["vision_last_store_ms_ago"] = lastVisualStoreMs > 0 ? (millis() - lastVisualStoreMs) : -1;
      doc["vision_store_count_last_hour"] = countVisualStoresLastHour(millis());
      String contextPreview = lastVisualContext;
      if (contextPreview.length() > 180) {
        contextPreview = contextPreview.substring(0, 180) + "...";
      }
      doc["vision_last_context_preview"] = contextPreview;
      String out;
      serializeJson(doc, out);
      return out;
    });
    webControl->setConfigHandlers(
      []() -> String {
        DynamicJsonDocument doc(1024);
        doc["ai_provider"] = ai_provider;
        doc["backend_api_url"] = backend_api_url;
        doc["use_backend_tts"] = use_backend_tts;
        doc["openai_model"] = openai_model;
        doc["tts_apiBaseUrl"] = tts_apiBaseUrl;
        doc["tts_openai_model"] = tts_openai_model;
        doc["use_elevenlabs_tts"] = use_elevenlabs_tts;
        doc["elevenlabs_voice_id"] = elevenlabs_voice_id;
        doc["elevenlabs_model_id"] = elevenlabs_model_id;
        doc["elevenlabs_output_format"] = elevenlabs_output_format;
        doc["gemini_model"] = gemini_model;
        doc["vision_enabled"] = vision_enabled;
        doc["vision_refresh_interval"] = vision_refresh_interval;
        doc["vision_capture_on_user_turn"] = vision_capture_on_user_turn;
        doc["vision_dedupe_threshold_pct"] = vision_dedupe_threshold_pct;
        doc["vision_min_store_interval_ms"] = vision_min_store_interval_ms;
        doc["vision_max_events_per_hour"] = vision_max_events_per_hour;
        doc["memory_mode"] = memory_mode;
        doc["web_control_enabled"] = web_control_enabled;
        String out;
        serializeJson(doc, out);
        return out;
      },
      [](const String& json) -> bool {
        DynamicJsonDocument doc(1024);
        if (deserializeJson(doc, json) != DeserializationError::Ok) {
          return false;
        }
        if (doc.containsKey("vision_enabled")) vision_enabled = doc["vision_enabled"].as<bool>();
        if (doc.containsKey("vision_refresh_interval")) vision_refresh_interval = doc["vision_refresh_interval"].as<unsigned long>();
        if (doc.containsKey("vision_capture_on_user_turn")) vision_capture_on_user_turn = doc["vision_capture_on_user_turn"].as<bool>();
        if (doc.containsKey("vision_dedupe_threshold_pct")) vision_dedupe_threshold_pct = doc["vision_dedupe_threshold_pct"].as<int>();
        if (doc.containsKey("vision_min_store_interval_ms")) vision_min_store_interval_ms = doc["vision_min_store_interval_ms"].as<unsigned long>();
        if (doc.containsKey("vision_max_events_per_hour")) vision_max_events_per_hour = doc["vision_max_events_per_hour"].as<int>();
        if (doc.containsKey("memory_mode")) memory_mode = doc["memory_mode"].as<String>();
        if (doc.containsKey("ai_provider")) ai_provider = doc["ai_provider"].as<String>();
        if (doc.containsKey("backend_api_url")) backend_api_url = doc["backend_api_url"].as<String>();
        if (doc.containsKey("use_backend_tts")) use_backend_tts = doc["use_backend_tts"].as<bool>();
        if (doc.containsKey("openai_model")) openai_model = doc["openai_model"].as<String>();
        if (doc.containsKey("tts_apiBaseUrl")) tts_apiBaseUrl = doc["tts_apiBaseUrl"].as<String>();
        if (doc.containsKey("tts_openai_model")) tts_openai_model = doc["tts_openai_model"].as<String>();
        if (doc.containsKey("use_elevenlabs_tts")) use_elevenlabs_tts = doc["use_elevenlabs_tts"].as<bool>();
        if (doc.containsKey("elevenlabs_voice_id")) elevenlabs_voice_id = doc["elevenlabs_voice_id"].as<String>();
        if (doc.containsKey("elevenlabs_model_id")) elevenlabs_model_id = doc["elevenlabs_model_id"].as<String>();
        if (doc.containsKey("elevenlabs_output_format")) elevenlabs_output_format = doc["elevenlabs_output_format"].as<String>();
        if (doc.containsKey("gemini_model")) gemini_model = doc["gemini_model"].as<String>();
        saveConfigToFlash();
        return true;
      }
    );
    webControl->setActionHandlers(
      []() -> bool {
        if (!continuousMode) startContinuousMode();
        return true;
      },
      []() -> bool {
        if (continuousMode) stopContinuousMode();
        return true;
      }
    );
    webControl->setPromptHandler([](const String& prompt) -> bool {
      if (prompt.length() == 0 || aiProvider == nullptr) return false;
      system_prompt = prompt;
      aiProvider->setSystemPrompt(system_prompt);
      saveConfigToFlash();
      return true;
    });
    webControl->setModelHandler([](const String& provider, const String& model) -> bool {
      if (provider == "backend") {
        if (backendProvider == nullptr) return false;
        ai_provider = "backend";
        aiProvider = backendProvider;
        if (model.length() > 0) {
          gemini_model = model;
          backendProvider->setModel(model);
        }
      } else if (provider == "gemini") {
        if (geminiProvider == nullptr) return false;
        ai_provider = "gemini";
        aiProvider = geminiProvider;
        if (model.length() > 0) {
          gemini_model = model;
          geminiProvider->setModel(model);
        }
      } else {
        if (openaiProvider == nullptr) return false;
        ai_provider = "openai";
        aiProvider = openaiProvider;
        if (model.length() > 0) {
          openai_model = model;
          openaiProvider->setModel(model);
        }
      }
      if (visualContextMgr != nullptr) {
        visualContextMgr->setProvider(aiProvider);
      }
      saveConfigToFlash();
      return true;
    });
    webControl->setMemoryHandlers(
      []() -> String {
        DynamicJsonDocument doc(512);
        doc["mode"] = memory_mode;
        doc["remote_memory_enabled"] = remoteMemory.isEnabled();
        String out;
        serializeJson(doc, out);
        return out;
      },
      []() -> bool {
        if (openaiProvider != nullptr) openaiProvider->clearMemory();
        if (geminiProvider != nullptr) geminiProvider->clearMemory();
        return true;
      }
    );

    if (webControl->begin()) {
      Serial.printf("Web control enabled on :%d (ws:%d)\n", web_port, web_ws_port);
    } else {
      Serial.println("Web control requested but unavailable (missing deps?)");
    }
  }
  
  Serial.println("\n----- System Ready -----");
  Serial.println("Press BOOT button to start/stop conversation");
  
  return true;
}

// ============================================================================
// TTS Callback Functions (for WebSocket TTS)
// ============================================================================

void onTTSComplete() {
  Serial.println("[TTS] WebSocket playback completed");
  ttsCompleted = true;
}

void onTTSError(const char* error) {
  Serial.printf("[TTS Error] %s\n", error);
  ttsCompleted = true;
}

// ============================================================================
// Continuous Conversation Mode Control Functions
// ============================================================================

void startContinuousMode() {
  continuousMode = true;
  currentState = STATE_LISTENING;
  
  Serial.println("\n========================================");
  if (single_turn_mode) {
    Serial.println("  Single-Turn Mode Started");
    if (manual_record_control) {
      Serial.println("  Type 'stop' to finalize this utterance");
    } else {
      Serial.println("  One utterance will be processed");
    }
  } else {
    Serial.println("  Continuous Conversation Mode Started");
    Serial.println("  Press BOOT again to stop");
  }
  Serial.println("========================================");
  
  if (asrStartRecording()) {
    Serial.println("\n[ASR] Listening... Please speak");
  } else {
    Serial.println("\n[Error] ASR startup failed");
    continuousMode = false;
    currentState = STATE_IDLE;
  }

  if (vision_enabled && vision_on_conversation_start && visualContextMgr != nullptr) {
    String ctx = visualContextMgr->captureAndDescribe(vision_prompt);
    applyVisualContext(ctx, "conversation_start");
  }
}

void stopContinuousMode() {
  continuousMode = false;
  
  Serial.println("\n========================================");
  Serial.println("  Continuous Conversation Mode Stopped");
  Serial.println("========================================");
  
  if (asrIsRecording()) {
    asrStopRecording();
  }
  
  currentState = STATE_IDLE;
  Serial.println("\nPress BOOT button to start conversation");
}

// ============================================================================
// ASR Result Processing Function
// ============================================================================

void handleASRResult() {
  if (aiProvider == nullptr) {
    Serial.println("[Error] AI provider not initialized");
    stopContinuousMode();
    return;
  }

  String transcribedText = asrGetRecognizedText();
  String userInputRaw = transcribedText;
  asrClearResult();
  
  if (transcribedText.length() > 0) {
    // ========== Display Recognition Result ==========
    Serial.println("\n=== ASR Recognition Result ===");
    Serial.printf("%s\n", transcribedText.c_str());
    Serial.println("==============================");
    
    // ========== Build contextual prompt ==========
    currentState = STATE_PROCESSING_LLM;
    Serial.println("\n[LLM] Sending request...");

    if (vision_enabled && vision_capture_on_user_turn && visualContextMgr != nullptr) {
      String ctx = visualContextMgr->captureAndDescribe(vision_prompt);
      applyVisualContext(ctx, "user_turn");
    }

    String resolvedPrompt = system_prompt;
    if (vision_enabled && lastVisualContext.length() > 0) {
      resolvedPrompt = buildContextAwarePrompt(system_prompt, lastVisualContext);
    }
    aiProvider->setSystemPrompt(resolvedPrompt);

    String recallText = "";
    if (isRemoteMemoryMode(memory_mode)) {
      recallText = remoteMemory.recall(transcribedText);
      if (recallText.length() > 0) {
        transcribedText += "\n\n[Relevant memory]\n" + recallText;
      }
    }

    String response = aiProvider->sendMessage(transcribedText);
    
    if (response != "" && response.length() > 0) {
      // ========== Display LLM Response ==========
      Serial.println("\n=== LLM Response ===");
      Serial.printf("%s\n", response.c_str());
      Serial.println("========================");
      
      // ========== Convert to Speech and Play ==========
      currentState = STATE_PLAYING_TTS;
      bool success = false;

      if (subscription == "pro") {
        // Pro: Use MiniMax WebSocket TTS
        Serial.println("\n[MiniMax TTS] Converting to speech (WebSocket)...");
        ttsCompleted = false;  // Reset completion flag
        success = ttsChat->speak(response.c_str());
      } else {
        // Free: Prefer backend proxy for stability, fallback to direct providers.
        if (use_backend_tts && backendTTS.isConfigured()) {
          Serial.println("\n[Backend TTS] Converting to speech...");
          success = backendTTS.speak(response);
        } else if (use_elevenlabs_tts) {
          Serial.println("\n[ElevenLabs TTS] Converting to speech...");
          success = elevenlabsTTS.speak(response);
        } else {
          Serial.println("\n[OpenAI TTS] Converting to speech...");
          if (ttsProvider != nullptr) {
            success = ttsProvider->client().textToSpeech(response);
          }
        }
      }
      
      if (success) {
        currentState = STATE_WAIT_TTS_COMPLETE;
        ttsStartTime = millis();
        ttsCheckTime = millis();
      } else {
        Serial.println("[Error] TTS playback failed");

        if (continuousMode && !single_turn_mode) {
          delay(500);
          currentState = STATE_LISTENING;
          if (asrStartRecording()) {
            Serial.println("\n[ASR] Listening... Please speak");
          } else {
            stopContinuousMode();
          }
        } else if (continuousMode && single_turn_mode) {
          stopContinuousMode();
        } else {
          currentState = STATE_IDLE;
        }
      }

      if (isRemoteMemoryMode(memory_mode)) {
        remoteMemory.storeConversation(userInputRaw, response, aiProvider->getProviderName(), lastVisualContext);
      }
    } else {
      Serial.println("[Error] Failed to get LLM response");

      if (continuousMode && !single_turn_mode) {
        delay(500);
        currentState = STATE_LISTENING;
        if (asrStartRecording()) {
          Serial.println("\n[ASR] Listening... Please speak");
        } else {
          stopContinuousMode();
        }
      } else if (continuousMode && single_turn_mode) {
        stopContinuousMode();
      } else {
        currentState = STATE_IDLE;
      }
    }
  } else {
    Serial.println("[Warning] No text recognized");

    if (continuousMode && !single_turn_mode) {
      delay(500);
      currentState = STATE_LISTENING;
      if (asrStartRecording()) {
        Serial.println("\n[ASR] Listening... Please speak");
      } else {
        stopContinuousMode();
      }
    } else if (continuousMode && single_turn_mode) {
      stopContinuousMode();
    } else {
      currentState = STATE_IDLE;
    }
  }
}

// ============================================================================
// Initialization Function
// ============================================================================

void setup() {
  // Increase serial RX buffer to handle long JSON strings
  Serial.setRxBufferSize(2048);  // Increase from default 256 to 2048 bytes
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n====================================================");
  Serial.println("   ESP32 Configurable Voice Assistant System");
  Serial.println("====================================================");
  
  // Initialize BOOT button
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  
  // Initialize random seed
  randomSeed(analogRead(0) + millis());
  
  // Try to load config from Flash
  Serial.println("\n[Startup] Checking Flash for config...");
  if (loadConfigFromFlash()) {
    configReceived = true;
    Serial.println("[Startup] Using config from Flash");
    Serial.println("\nTip: Send new JSON config via serial before pressing BOOT to update");
  } else {
    Serial.println("[Startup] No config in Flash, waiting for serial config...");
    Serial.println("\nFree version config example:");
    Serial.println("{\"wifi_ssid\":\"YourWiFi\",\"wifi_password\":\"YourPassword\",\"subscription\":\"free\",\"asr_provider\":\"backend\",\"asr_api_url\":\"http://YOUR_LAPTOP_IP:8787\",\"asr_api_key\":\"same-as-memory-api-key\",\"single_turn_mode\":true,\"manual_record_control\":true,\"ai_provider\":\"backend\",\"backend_api_url\":\"http://YOUR_LAPTOP_IP:8787\",\"backend_api_key\":\"same-as-memory-api-key\",\"gemini_model\":\"gemini-2.0-flash\",\"openai_apiKey\":\"not-used\",\"use_backend_tts\":true,\"elevenlabs_voice_id\":\"EST9Ui6982FZPSi7gCHi\",\"elevenlabs_model_id\":\"eleven_flash_v2_5\",\"system_prompt\":\"You are a helpful assistant.\",\"vision_enabled\":false,\"memory_mode\":\"local\"}");
    Serial.println("\nBackend ASR proxy example:");
    Serial.println("{\"wifi_ssid\":\"YourWiFi\",\"wifi_password\":\"YourPassword\",\"subscription\":\"free\",\"asr_provider\":\"backend\",\"asr_api_url\":\"http://YOUR_LAPTOP_IP:8787\",\"asr_api_key\":\"same-as-memory-api-key\",\"ai_provider\":\"backend\",\"backend_api_url\":\"http://YOUR_LAPTOP_IP:8787\",\"backend_api_key\":\"same-as-memory-api-key\",\"gemini_model\":\"gemini-2.0-flash\",\"openai_apiKey\":\"not-used\",\"use_backend_tts\":true,\"elevenlabs_voice_id\":\"EST9Ui6982FZPSi7gCHi\",\"elevenlabs_model_id\":\"eleven_flash_v2_5\",\"system_prompt\":\"You are a helpful assistant.\",\"vision_enabled\":false,\"memory_mode\":\"local\"}");
    Serial.println("\nPro version config example:");
    Serial.println("{\"wifi_ssid\":\"YourWiFi\",\"wifi_password\":\"YourPassword\",\"subscription\":\"pro\",\"asr_provider\":\"gemini\",\"single_turn_mode\":true,\"manual_record_control\":true,\"ai_provider\":\"gemini\",\"gemini_apiKey\":\"your-gemini-key\",\"gemini_model\":\"gemini-2.0-flash\",\"openai_apiKey\":\"your-openai-key\",\"openai_apiBaseUrl\":\"https://api.openai.com\",\"system_prompt\":\"You are a helpful assistant.\",\"minimax_apiKey\":\"your-key\",\"minimax_groupId\":\"your-id\",\"tts_voice_id\":\"female-tianmei\",\"memory_api_url\":\"https://your-memory-api.example.com\",\"memory_api_key\":\"api-key\",\"memory_mode\":\"both\",\"web_control_enabled\":true}");
  }
  
  Serial.println("\nPress BOOT button to start system...\n");
}

// ============================================================================
// Main Loop Function
// ============================================================================

void loop() {
  // ========== Waiting for Config State ==========
  if (currentState == STATE_WAITING_CONFIG) {
    // Check for new serial config
    if (receiveConfig()) {
      configReceived = true;
      Serial.println("\nNew config received!");
      Serial.println("Press BOOT to test and save new config...");
    }
    
    // Detect BOOT button, initialize system and start directly
    buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (buttonPressed && !wasButtonPressed && configReceived && !systemInitialized) {
      wasButtonPressed = true;
      
      Serial.println("\n[Startup] Initializing system...");
      if (initializeSystem()) {
        systemInitialized = true;
        
        // Init successful, save config to Flash
        saveConfigToFlash();
        currentState = STATE_IDLE;
        Serial.println("\n[Startup] Ready. Press BOOT to start conversation, or type 'start' in Serial.");
      } else {
        Serial.println("\n[Error] System init failed, check config");
        Serial.println("Config not saved to Flash. You can fix wiring/network and press BOOT to retry.");
        currentState = STATE_WAITING_CONFIG;
      }
    } else if (!buttonPressed && wasButtonPressed) {
      wasButtonPressed = false;
    }
    
    delay(100);
    return;
  }

  // Runtime command parsing is enabled only after system init.
  // This avoids consuming JSON provisioning input during WAITING_CONFIG.
  processRuntimeCommand();
  
  // ========== Process TTS/Audio Loop ==========
  if (subscription == "pro" && ttsChat != nullptr) {
    // WebSocket TTS: process WebSocket messages (audio playback is handled by FreeRTOS task)
    ttsChat->loop();
  } else {
    // Free mode: process Audio library loop
    audio.loop();
  }

  // ========== Process ASR Loop ==========
  asrLoop();

  if (webControl != nullptr) {
    webControl->loop();
  }

  if (vision_enabled && continuousMode && visualContextMgr != nullptr && currentState != STATE_PROCESSING_LLM) {
    if (millis() - lastVisionRefresh >= vision_refresh_interval) {
      String ctx = visualContextMgr->captureAndDescribe(vision_prompt);
      applyVisualContext(ctx, "periodic");
    }
  }
  
  // ========== Process BOOT Button (Toggle start/stop) ==========
  buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  
  if (buttonPressed && !wasButtonPressed) {
    wasButtonPressed = true;
    
    if (continuousMode) {
      stopContinuousMode();
    } else if (systemInitialized) {
      startContinuousMode();
    }
  } else if (!buttonPressed && wasButtonPressed) {
    wasButtonPressed = false;
  }
  
  // ========== State Machine Processing ==========
  switch (currentState) {
    case STATE_IDLE:
      break;
      
    case STATE_LISTENING:
      if (asrHasNewResult()) {
        handleASRResult();
      }
      break;
      
    case STATE_PROCESSING_LLM:
      break;
      
    case STATE_PLAYING_TTS:
      break;
      
    case STATE_WAIT_TTS_COMPLETE:
      if (millis() - ttsCheckTime > 100) {
        ttsCheckTime = millis();

        // Check completion based on subscription type
        bool playbackComplete = false;
        if (subscription == "pro") {
          // WebSocket TTS: check callback flag or isPlaying
          playbackComplete = ttsCompleted || !ttsChat->isPlaying();
        } else {
          // OpenAI TTS: check Audio library
          playbackComplete = !audio.isRunning();
        }

        if (playbackComplete) {
          Serial.println("[TTS] Playback completed");

          if (continuousMode && !single_turn_mode) {
            delay(500);
            currentState = STATE_LISTENING;

            if (asrStartRecording()) {
              Serial.println("\n[ASR] Listening... Please speak");
            } else {
              Serial.println("[Error] ASR restart failed");
              stopContinuousMode();
            }
          } else if (continuousMode && single_turn_mode) {
            stopContinuousMode();
          } else {
            currentState = STATE_IDLE;
          }
        } else {
          // Check timeout
          if (millis() - ttsStartTime > 60000) {
            Serial.println("[Warning] TTS timeout, forcing restart");

            if (subscription == "pro" && ttsChat != nullptr) {
              ttsChat->stop();  // Stop WebSocket TTS
            }

            if (continuousMode) {
              currentState = STATE_LISTENING;
              if (asrStartRecording()) {
                Serial.println("\n[ASR] Listening... Please speak");
              } else {
                stopContinuousMode();
              }
            } else {
              currentState = STATE_IDLE;
            }
          }
        }
      }
      break;
      
    case STATE_WAITING_CONFIG:
      // Already handled above
      break;
  }
  
  // ========== Loop Delay Control ==========
  if (currentState == STATE_LISTENING ||
      currentState == STATE_PLAYING_TTS ||
      currentState == STATE_WAIT_TTS_COMPLETE) {
    // Fast loop for audio streaming - use minimal delay
    delay(1);
  } else {
    delay(10);
  }
}
