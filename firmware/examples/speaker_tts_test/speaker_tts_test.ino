/*
 * ESP32 Speaker TTS Test (No Mic Required)
 *
 * Flow:
 * 1) Send one-line JSON config over serial
 * 2) Press BOOT to initialize WiFi + speaker
 * 3) Board speaks test sentence through MAX98357
 * 4) Press BOOT again to replay
 *
 * JSON example:
 * {"wifi_ssid":"MyHotspot","wifi_password":"MyPass","tts_api_key":"sk-...","tts_api_base_url":"https://api.openai.com","tts_model":"gpt-4o-mini-tts","tts_voice":"alloy","tts_speed":"1.0","speak_text":"Hello this is your personal AI assistance through the speaker.","i2s_bclk":26,"i2s_lrc":25,"i2s_dout":22}
 */

#include <WiFi.h>
#include <ArduinoJson.h>
#include <OpenAIProvider.h>
#include "Audio.h"

// Typical ESP32 defaults for MAX98357 (change by JSON if needed)
int I2S_BCLK = 26;
int I2S_LRC = 25;
int I2S_DOUT = 22;

#define BOOT_BUTTON_PIN 0

String wifi_ssid = "";
String wifi_password = "";
String tts_api_key = "";
String tts_api_base_url = "https://api.openai.com";
String tts_model = "gpt-4o-mini-tts";
String tts_voice = "alloy";
String tts_speed = "1.0";
String speak_text = "Hello this is your personal AI assistance through the speaker.";
int volume = 80;

Audio audio;
OpenAIProvider* ttsProvider = nullptr;

bool configReady = false;
bool initialized = false;
bool wasButtonPressed = false;
bool speechInProgress = false;
unsigned long speechStartMs = 0;
unsigned long lastPlaybackDebugMs = 0;

// Audio library debug callbacks (weak symbols overridden by this sketch)
void audio_info(const char* info) {
  Serial.print("[Audio][info] ");
  Serial.println(info);
}

void audio_log(uint8_t logLevel, const char* msg, const char* arg) {
  Serial.printf("[Audio][log][%u] %s %s\n", logLevel, msg ? msg : "", arg ? arg : "");
}

void audio_eof_speech(const char* info) {
  Serial.printf("[Audio] EOF speech: %s\n", info ? info : "");
  speechInProgress = false;
}

void audio_showstreamtitle(const char* info) {
  Serial.printf("[Audio] Stream title: %s\n", info ? info : "");
}

void audio_bitrate(const char* info) {
  Serial.printf("[Audio] Bitrate: %s\n", info ? info : "");
}

bool receiveConfig() {
  if (!Serial.available()) {
    return false;
  }

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) {
    return false;
  }

  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, line) != DeserializationError::Ok) {
    Serial.println("[Config] Invalid JSON");
    return false;
  }

  wifi_ssid = doc["wifi_ssid"] | wifi_ssid;
  wifi_password = doc["wifi_password"] | wifi_password;
  tts_api_key = doc["tts_api_key"] | tts_api_key;
  tts_api_base_url = doc["tts_api_base_url"] | tts_api_base_url;
  tts_model = doc["tts_model"] | tts_model;
  tts_voice = doc["tts_voice"] | tts_voice;
  tts_speed = doc["tts_speed"] | tts_speed;
  speak_text = doc["speak_text"] | speak_text;
  if (doc.containsKey("volume")) volume = doc["volume"].as<int>();

  if (doc.containsKey("i2s_bclk")) I2S_BCLK = doc["i2s_bclk"].as<int>();
  if (doc.containsKey("i2s_lrc")) I2S_LRC = doc["i2s_lrc"].as<int>();
  if (doc.containsKey("i2s_dout")) I2S_DOUT = doc["i2s_dout"].as<int>();

  if (wifi_ssid.length() == 0 || wifi_password.length() == 0 || tts_api_key.length() == 0) {
    Serial.println("[Config] Missing required fields: wifi_ssid, wifi_password, tts_api_key");
    return false;
  }

  configReady = true;
  Serial.println("[Config] OK. Press BOOT to initialize and speak.");
  return true;
}

bool initializeSystem() {
  Serial.println("[Init] Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid.c_str(), wifi_password.c_str());

  int attempt = 0;
  while (WiFi.status() != WL_CONNECTED && attempt < 20) {
    Serial.print('.');
    delay(1000);
    attempt++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Init] WiFi failed");
    return false;
  }

  Serial.println("[Init] WiFi connected");
  Serial.print("[Init] IP: ");
  Serial.println(WiFi.localIP());

  if (!audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT)) {
    Serial.println("[Init] Audio pinout failed");
    return false;
  }
  volume = constrain(volume, 0, 100);
  audio.setVolume(volume);
  Serial.printf("[Init] Volume=%d\n", volume);

  ttsProvider = new OpenAIProvider(tts_api_key.c_str(), tts_api_base_url.c_str());
  ttsProvider->client().setTTSConfig(tts_model.c_str(), tts_voice.c_str(), tts_speed.c_str());

  Serial.printf("[Init] TTS base=%s model=%s voice=%s\n",
                tts_api_base_url.c_str(), tts_model.c_str(), tts_voice.c_str());
  Serial.printf("[Init] I2S BCLK=%d LRC=%d DOUT=%d\n", I2S_BCLK, I2S_LRC, I2S_DOUT);
  return true;
}

void speakNow() {
  if (ttsProvider == nullptr) {
    Serial.println("[Speak] TTS provider not ready");
    return;
  }

  Serial.println("[Speak] Sending TTS request...");
  Serial.printf("[Speak] Text length=%d\n", (int)speak_text.length());
  Serial.printf("[Speak] Free heap before request=%u\n", ESP.getFreeHeap());
  bool ok = ttsProvider->client().textToSpeech(speak_text);
  if (!ok) {
    Serial.println("[Speak] TTS request failed");
    return;
  }

  speechInProgress = true;
  speechStartMs = millis();
  lastPlaybackDebugMs = 0;
  Serial.println("[Speak] TTS accepted. Waiting for audio callbacks...");
}

void setup() {
  Serial.begin(115200);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  Serial.println("\n=== Speaker TTS Test ===");
  Serial.println("Send JSON config in one line, then press BOOT.");
  Serial.println("Example:");
  Serial.println("{\"wifi_ssid\":\"MyHotspot\",\"wifi_password\":\"MyPass\",\"tts_api_key\":\"sk-...\",\"tts_api_base_url\":\"https://api.openai.com\",\"tts_model\":\"gpt-4o-mini-tts\",\"tts_voice\":\"alloy\",\"tts_speed\":\"1.0\",\"speak_text\":\"Hello this is your personal AI assistance through the speaker.\",\"i2s_bclk\":26,\"i2s_lrc\":25,\"i2s_dout\":22}");
}

void loop() {
  receiveConfig();
  audio.loop();

  if (speechInProgress && millis() - lastPlaybackDebugMs > 1000) {
    lastPlaybackDebugMs = millis();
    Serial.printf("[Playback] running=%s current=%lus duration=%lus free_heap=%u\n",
                  audio.isRunning() ? "true" : "false",
                  (unsigned long)audio.getAudioCurrentTime(),
                  (unsigned long)audio.getAudioFileDuration(),
                  ESP.getFreeHeap());
    if (!audio.isRunning() && millis() - speechStartMs > 3000) {
      Serial.println("[Playback] Audio not running. Check wiring/power/pins to amplifier.");
      speechInProgress = false;
    }
  }

  bool buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
  if (buttonPressed && !wasButtonPressed) {
    wasButtonPressed = true;

    if (!configReady) {
      Serial.println("[Action] Send valid JSON config first");
      return;
    }

    if (!initialized) {
      initialized = initializeSystem();
      if (!initialized) {
        Serial.println("[Action] Init failed. Fix config/wiring and press BOOT again.");
        return;
      }
    }

    speakNow();
  } else if (!buttonPressed && wasButtonPressed) {
    wasButtonPressed = false;
  }
}
