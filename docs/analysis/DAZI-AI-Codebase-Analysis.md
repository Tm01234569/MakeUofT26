# DAZI-AI Codebase Analysis & Architecture Plan

**Date:** 2026-02-14
**Project:** Mini Portable Girlfriend Bot (MakeUofT Hackathon)
**Team:** Sho, Tomo, Kenneth
**Base Library:** [DAZI-AI](https://github.com/dazi-ai/DAZI-AI) v1.0.0

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Project Structure](#2-project-structure)
3. [Architecture Overview](#3-architecture-overview)
4. [Core Library Breakdown](#4-core-library-breakdown)
5. [Example Programs](#5-example-programs)
6. [Hardware Pin Mapping](#6-hardware-pin-mapping)
7. [API Endpoints & Communication](#7-api-endpoints--communication)
8. [Audio Pipeline](#8-audio-pipeline)
9. [Current Memory System](#9-current-memory-system)
10. [Code Quality & Security Review](#10-code-quality--security-review)
11. [Planned Modifications](#11-planned-modifications)
12. [Gemini Integration Plan](#12-gemini-integration-plan)
13. [Computer Vision & Face Tracking](#13-computer-vision--face-tracking)
14. [Supabase Memory Architecture](#14-supabase-memory-architecture)
15. [Web App Control Interface](#15-web-app-control-interface)
16. [Serverless Architecture](#16-serverless-architecture)
17. [Dependencies](#17-dependencies)
18. [Recommended Starting Example](#18-recommended-starting-example)

---

## 1. Executive Summary

**DAZI-AI** is a serverless AI voice assistant library for ESP32. It enables real-time voice conversations (voice-to-text-to-voice) **without requiring a backend server** — the ESP32 talks directly to cloud APIs via HTTPS/WebSocket.

### What It Does Today
- **ASR (Speech-to-Text):** ByteDance Volcengine via WebSocket
- **LLM (AI Brain):** OpenAI ChatGPT via HTTP REST
- **TTS (Text-to-Speech):** OpenAI TTS (free) or MiniMax (pro) via WebSocket
- **Conversation Memory:** In-memory, max 5 exchanges (no persistence)

### What We Need to Add
| Feature | Status | Priority | Owner |
|---------|--------|----------|-------|
| AI Model Selection (OpenAI/Gemini) | Not started | High | Sho |
| Gemini Vision (multimodal) | Not started | High | Sho |
| Gemini Visual Context Awareness | Not started | High | Sho |
| Computer Vision (face tracking) | In progress (separate) | High | Tomo + Kenneth |
| Servo head movement (pan/tilt) | In progress (separate) | High | Tomo + Kenneth |
| Short-term memory (ESP32) | Partial (5 pairs in RAM) | Medium | Sho |
| Long-term memory (Supabase) | Not started | High | Sho |
| Web App control interface | Not started | High | Sho |
| OLED face animations | Separate from DAZI-AI | Medium | Kenneth |
| Serverless (no PC intermediary) | Already serverless | Done | - |

---

## 2. Project Structure

```
DAZI-AI-main/
├── README.md                              # Main documentation
├── library.properties                     # Arduino library metadata
├── keywords.txt                           # Arduino IDE syntax highlighting
│
├── src/                                   # Core library
│   ├── ArduinoGPTChat.h/cpp              # OpenAI ChatGPT + TTS + STT
│   ├── ArduinoASRChat.h/cpp              # ByteDance ASR (WebSocket)
│   ├── ArduinoTTSChat.h/cpp              # MiniMax TTS (WebSocket streaming)
│   ├── ArduinoMinimaxTTS.h/cpp           # MiniMax TTS (HTTP REST, legacy)
│   ├── ArduinoRealtimeDialog.h/cpp       # Volcengine Doubao end-to-end
│   ├── Audio.h/cpp                       # Modified ESP32-audioI2S library
│   ├── I2SAudioPlayer.h/cpp              # Low-level I2S PCM player
│   ├── mp3_decoder/                      # Helix MP3 decoder
│   ├── aac_decoder/                      # FAAD AAC decoder
│   ├── flac_decoder/                     # FLAC decoder
│   ├── opus_decoder/                     # Opus (CELT+SILK) decoder
│   └── vorbis_decoder/                   # OGG Vorbis decoder
│
├── examples/                              # Example sketches
│   ├── chat/                             # Basic push-to-talk (simplest)
│   ├── chat_asr/                         # Continuous conversation + VAD
│   ├── chat_configurable/                # JSON config, Free/Pro TTS ★
│   ├── chat_configurable_m5cores3/       # M5CoreS3 version
│   ├── chat_minimax_tts/                 # OpenAI + MiniMax TTS
│   ├── RealtimeDialog/                   # Volcengine end-to-end
│   ├── tts_websocket/                    # WebSocket TTS only
│   └── tts_websocket_m5cores3/           # M5CoreS3 WebSocket TTS
│
├── docs/
│   └── M5CoreS3_Notes.md                 # Chinese M5CoreS3 docs
│
└── img/                                   # Diagrams and photos
```

**Total:** 54 files, 7.6 MB
**Key insight:** The `chat_configurable` example is the most complete and our best starting point.

---

## 3. Architecture Overview

### Current Data Flow

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐     ┌─────────────┐
│  User Speaks │────▶│  INMP441 Mic │────▶│  ByteDance  │────▶│   OpenAI    │
│              │     │  (I2S Input) │     │  ASR (WSS)  │     │  ChatGPT   │
└─────────────┘     └──────────────┘     └──────────────┘     └──────┬──────┘
                                                                      │
┌─────────────┐     ┌──────────────┐     ┌─────────────┐             │
│  User Hears │◀────│  MAX98357A   │◀────│  TTS Engine │◀────────────┘
│              │     │  (I2S Output)│     │ (OpenAI/MM) │
└─────────────┘     └──────────────┘     └─────────────┘
```

### State Machine (from chat_configurable.ino)

```
STATE_WAITING_CONFIG ──(BOOT button)──▶ STATE_IDLE
                                            │
                                    (start continuous)
                                            │
                                            ▼
                                     STATE_LISTENING
                                            │
                                      (ASR result)
                                            │
                                            ▼
                                   STATE_PROCESSING_LLM
                                            │
                                    (ChatGPT response)
                                            │
                                            ▼
                                    STATE_PLAYING_TTS
                                            │
                                            ▼
                                  STATE_WAIT_TTS_COMPLETE
                                            │
                                     (loop back)
                                            │
                                            ▼
                                     STATE_LISTENING
```

### Planned Architecture (After Modifications)

```
┌──────────────────────────────────────────────────────────────────┐
│                        ESP32-S3 CAM                              │
│                                                                  │
│  ┌─────────┐  ┌─────────┐  ┌──────────┐  ┌───────────────────┐ │
│  │ INMP441 │  │ Camera  │  │ OLED     │  │ 2x SG90 Servos   │ │
│  │ Mic     │  │ Module  │  │ Display  │  │ (Head Movement)   │ │
│  └────┬────┘  └────┬────┘  └────▲─────┘  └────▲──────────────┘ │
│       │            │            │              │                 │
│  ┌────▼────────────▼────────────┴──────────────┴───────────┐    │
│  │              Main Controller (Arduino Loop)              │    │
│  │  ┌──────────────────────────────────────────────────┐   │    │
│  │  │         AI Provider Abstraction Layer            │   │    │
│  │  │  ┌──────────┐  ┌──────────┐  ┌──────────────┐  │   │    │
│  │  │  │  OpenAI  │  │  Gemini  │  │  Gemini      │  │   │    │
│  │  │  │  (Chat)  │  │  (Chat)  │  │  (Vision)    │  │   │    │
│  │  │  └──────────┘  └──────────┘  └──────────────┘  │   │    │
│  │  └──────────────────────────────────────────────────┘   │    │
│  │  ┌──────────────────────────────────────────────────┐   │    │
│  │  │              Memory Manager                      │   │    │
│  │  │  ┌──────────────┐  ┌────────────────────────┐   │   │    │
│  │  │  │  Short-Term  │  │  Long-Term             │   │   │    │
│  │  │  │  (ESP32 RAM) │  │  (Supabase REST API)   │   │   │    │
│  │  │  └──────────────┘  └────────────────────────┘   │   │    │
│  │  └──────────────────────────────────────────────────┘   │    │
│  │  ┌──────────────────────────────────────────────────┐   │    │
│  │  │           Web Server (AsyncWebServer)            │   │    │
│  │  │  REST API + WebSocket + Hosted Web App           │   │    │
│  │  └──────────────────────────────────────────────────┘   │    │
│  └─────────────────────────────────────────────────────────┘    │
│       │                                                         │
│  ┌────▼────┐                                                    │
│  │MAX98357A│                                                    │
│  │ Speaker │                                                    │
│  └─────────┘                                                    │
└──────────────────────────────────────────────────────────────────┘
         │                    │                    │
         ▼                    ▼                    ▼
   ┌───────────┐     ┌──────────────┐     ┌──────────────┐
   │ ByteDance │     │   Google     │     │   Supabase   │
   │ ASR (WSS) │     │ Gemini API   │     │  REST API    │
   └───────────┘     └──────────────┘     └──────────────┘
```

---

## 4. Core Library Breakdown

### ArduinoGPTChat (src/ArduinoGPTChat.h/.cpp)

**Purpose:** OpenAI ChatGPT integration — chat, TTS, and STT

| Method | Purpose |
|--------|---------|
| `sendMessage(text)` | Send chat message, get AI response |
| `textToSpeech(text)` | Convert text to speech via OpenAI TTS |
| `speechToText(filePath)` | Whisper STT from audio file |
| `sendImageMessage(imagePath, question)` | Vision — send image + text to GPT-4V |
| `enableMemory(bool)` | Toggle conversation memory |
| `clearMemory()` | Clear conversation history |
| `setSystemPrompt(prompt)` | Set AI personality |
| `setApiConfig(key, baseUrl)` | Configure API endpoint |

**Key internals:**
- Model hardcoded to `gpt-4.1-nano` (line 609) — **needs to be configurable**
- TTS model: `gpt-4o-mini-tts` (line 677)
- Memory: `std::vector<std::pair<String, String>>`, max 5 pairs
- Has image support with base64 encoding
- `_updateApiUrls()` builds `/v1/chat/completions`, `/v1/audio/speech`, `/v1/audio/transcriptions`

### ArduinoASRChat (src/ArduinoASRChat.h/.cpp)

**Purpose:** Real-time speech recognition via ByteDance Volcengine WebSocket

| Method | Purpose |
|--------|---------|
| `initINMP441Microphone(sck, ws, sd)` | Init external I2S mic |
| `connectWebSocket()` | Connect to ASR service |
| `startRecording()` | Begin audio capture + streaming |
| `stopRecording()` | Stop and finalize |
| `getRecognizedText()` | Get transcription result |
| `setSilenceDuration(ms)` | VAD silence threshold (default: 1000ms) |
| `setMaxRecordingSeconds(sec)` | Max recording time (default: 50s) |
| `loop()` | Process WebSocket messages |

**Key internals:**
- Sends audio in 3200-byte batches (200ms at 16kHz)
- VAD with configurable silence detection
- Cluster: `volcengine_input_en` for English
- WebSocket binary protocol with custom header format

### ArduinoTTSChat (src/ArduinoTTSChat.h/.cpp)

**Purpose:** MiniMax streaming TTS via WebSocket (Pro tier)

| Method | Purpose |
|--------|---------|
| `initMAX98357Speaker(bclk, lrc, dout)` | Init I2S speaker |
| `connectWebSocket()` | Connect to MiniMax |
| `speak(text)` | Synthesize and play |
| `isPlaying()` | Check playback status |
| `stop()` | Stop playback |
| `setVoiceId(voice)` | Set TTS voice |
| `setSpeed(speed)` / `setVolume(vol)` | Audio parameters |

**Key internals:**
- 512KB ring buffer for streaming audio (PSRAM preferred)
- FreeRTOS task for concurrent audio playback
- Supports multiple voices and emotion control
- WebSocket protocol: `task_start` → `task_continue` → `task_finish`

### Audio.h/.cpp (Modified ESP32-audioI2S)

**Purpose:** Full audio playback library with multi-codec support

**Supported formats:** MP3, AAC, FLAC, Opus, Vorbis, WAV, PCM

This is a heavily modified fork of the `ESP32-audioI2S` library used by the "Free" TTS tier (OpenAI TTS returns MP3 URLs which this library streams and decodes).

---

## 5. Example Programs

| Example | Best For | ASR | LLM | TTS | Config |
|---------|----------|-----|-----|-----|--------|
| `chat` | Simplest demo | OpenAI Whisper | ChatGPT | OpenAI | Hardcoded |
| `chat_asr` | Continuous mode | ByteDance | ChatGPT | OpenAI | Hardcoded |
| **`chat_configurable`** | **Our base** | **ByteDance** | **ChatGPT** | **Both** | **JSON + Flash** |
| `chat_minimax_tts` | MiniMax demo | ByteDance | ChatGPT | MiniMax REST | Hardcoded |
| `RealtimeDialog` | End-to-end | Volcengine | Doubao | Doubao | Hardcoded |

**Recommendation:** Start from `chat_configurable` — it has JSON configuration, Flash persistence, Free/Pro TTS switching, and a clean state machine.

---

## 6. Hardware Pin Mapping

### Our ESP32-S3 CAM Configuration

| Component | Pin | GPIO | Notes |
|-----------|-----|------|-------|
| **Speaker (MAX98357A)** | | | |
| Data Out (DOUT) | I2S_DOUT | GPIO 47 | Audio data |
| Bit Clock (BCLK) | I2S_BCLK | GPIO 48 | Serial clock |
| L/R Clock (LRC) | I2S_LRC | GPIO 45 | Word select |
| **Microphone (INMP441)** | | | |
| Serial Clock (SCK) | I2S_MIC_SCK | GPIO 5 | Clock |
| Word Select (WS) | I2S_MIC_WS | GPIO 4 | L/R select |
| Serial Data (SD) | I2S_MIC_SD | GPIO 6 | Audio data |
| L/R | - | GND | Left channel |
| VDD | - | 3.3V | **NOT 5V** |
| **Control** | | | |
| Talk Button | BOOT_BUTTON | GPIO 0 | Press to talk |
| **OLED Display** | | | |
| SDA | - | TBD | I2C data |
| SCL | - | TBD | I2C clock |
| **Servos (SG90)** | | | |
| Servo X (Pan) | - | TBD | PWM signal |
| Servo Y (Tilt) | - | TBD | PWM signal |

**Note:** Camera pins are fixed on ESP32-S3 CAM and cannot be changed. Check the board's pinout diagram for available GPIOs.

---

## 7. API Endpoints & Communication

### Current APIs (All Serverless - ESP32 Direct)

| Service | Protocol | Host | Path | Auth |
|---------|----------|------|------|------|
| **ByteDance ASR** | WebSocket (TLS) | `openspeech.bytedance.com:443` | `/api/v2/asr` | API key in payload |
| **OpenAI Chat** | HTTP REST (TLS) | `api.openai.com:443` | `/v1/chat/completions` | Bearer token |
| **OpenAI TTS** | HTTP REST (TLS) | `api.openai.com:443` | `/v1/audio/speech` | Bearer token |
| **OpenAI STT** | HTTP REST (TLS) | `api.openai.com:443` | `/v1/audio/transcriptions` | Bearer token |
| **MiniMax TTS** | WebSocket (TLS) | `api.minimaxi.com:443` | `/ws/v1/t2a_v2` | JWT token |

### APIs We Need to Add

| Service | Protocol | Host | Path | Auth |
|---------|----------|------|------|------|
| **Gemini Chat** | HTTP REST (TLS) | `generativelanguage.googleapis.com` | `/v1beta/models/gemini-2.0-flash:generateContent` | API key in URL |
| **Gemini Vision** | HTTP REST (TLS) | `generativelanguage.googleapis.com` | `/v1beta/models/gemini-2.0-flash:generateContent` | API key in URL |
| **Supabase REST** | HTTP REST (TLS) | `<project>.supabase.co` | `/rest/v1/<table>` | API key header |

---

## 8. Audio Pipeline

### Recording (Mic → ASR)
```
INMP441 ──I2S──▶ ESP32 Buffer (320 samples / 20ms)
                      │
                 Batch to 3200 bytes (200ms)
                      │
                 WebSocket ──▶ ByteDance ASR
                      │
                 JSON response ◀── {"text": "Hello"}
```

### Playback (TTS → Speaker)

**Free Mode (OpenAI TTS):**
```
ChatGPT Response ──HTTP POST──▶ OpenAI TTS API
                                     │
                               MP3 URL response
                                     │
                Audio.h ◀── connecttohost(url) ──▶ Stream decode ──I2S──▶ MAX98357A
```

**Pro Mode (MiniMax WebSocket):**
```
ChatGPT Response ──WebSocket──▶ MiniMax TTS
                                     │
                         Hex-encoded PCM chunks
                                     │
                Ring Buffer (512KB PSRAM) ──FreeRTOS task──I2S──▶ MAX98357A
```

### Audio Codec Support
- **Playback:** MP3, AAC, FLAC, Opus, Vorbis, WAV, PCM
- **Recording:** Raw PCM 16-bit, 16kHz
- **PSRAM usage:** Ring buffer 512KB, audio decode buffers ~64KB

---

## 9. Current Memory System

### What Exists

```cpp
// ArduinoGPTChat.h:52-54
bool _memoryEnabled = false;
std::vector<std::pair<String, String>> _conversationHistory;
const int _maxHistoryPairs = 5;
```

- Stores last 5 user-assistant message pairs in RAM
- Sent as part of OpenAI chat payload (messages array)
- **Volatile** — lost on reboot/power cycle
- No Supabase, no SPIFFS persistence

### Memory Sent to LLM

```json
{
  "messages": [
    {"role": "system", "content": "You are..."},
    {"role": "user", "content": "msg 1"},
    {"role": "assistant", "content": "response 1"},
    {"role": "user", "content": "msg 2"},
    {"role": "assistant", "content": "response 2"},
    {"role": "user", "content": "current message"}
  ]
}
```

---

## 10. Code Quality & Security Review

### Critical Issues

| Issue | Severity | Location | Impact |
|-------|----------|----------|--------|
| **SSL verification disabled** | CRITICAL | All `setInsecure()` calls across src/ | MITM attacks can intercept API keys and audio |
| **API keys in plaintext** | HIGH | All example .ino files | Keys exposed in source code |
| **Buffer overflow risk** | HIGH | `ArduinoGPTChat.cpp:814-818` | No bounds checking on memcpy |
| **Memory leak in error paths** | MEDIUM | `ArduinoGPTChat.cpp:132-186` | Heap fragmentation over time |
| **Race condition in audio** | MEDIUM | `ArduinoTTSChat.h:228-239` | `volatile` != thread-safe |

### Architecture Gaps

| Gap | Impact | Fix Required |
|-----|--------|-------------|
| **No AI provider abstraction** | Can't switch models without recompile | Create AIProvider interface |
| **Hardcoded model names** | `gpt-4.1-nano` in source | Make configurable |
| **No persistent memory** | Memory lost on reboot | Add Supabase/SPIFFS |
| **No web interface** | Can't control remotely | Add AsyncWebServer |
| **No vision integration** | Can't use camera for AI | Add Gemini Vision |

### What's Good

- Clean state machine in `chat_configurable`
- JSON config with Flash persistence — extensible
- WebSocket streaming for low-latency TTS
- PSRAM-aware memory allocation with fallback
- Modular library structure (ASR, LLM, TTS are separate)

---

## 11. Planned Modifications

### 11.1 AI Provider Abstraction Layer

**Problem:** `ArduinoGPTChat` is tightly coupled to OpenAI's API format.

**Solution:** Create an abstract interface so we can swap between OpenAI and Gemini.

```cpp
// New: AIProvider.h
class AIProvider {
public:
    virtual String sendMessage(const String& message) = 0;
    virtual String sendVisionMessage(const uint8_t* imageData, size_t imageSize,
                                     const String& question) = 0;
    virtual String getModelName() const = 0;
    virtual ~AIProvider() = default;

    // Memory integration
    void setConversationHistory(const std::vector<std::pair<String, String>>& history);
    void setSystemPrompt(const String& prompt);

protected:
    String _systemPrompt;
    String _apiKey;
    std::vector<std::pair<String, String>> _history;
};

class OpenAIProvider : public AIProvider { /* existing GPTChat logic */ };
class GeminiProvider : public AIProvider { /* new Gemini logic */ };
```

**Files to modify:**
- `src/ArduinoGPTChat.h` — extract interface
- `src/ArduinoGPTChat.cpp:562-591` — refactor sendMessage
- `examples/chat_configurable.ino:447` — select provider at init

**Config addition:**
```json
{
  "ai_provider": "gemini",
  "gemini_apiKey": "your-gemini-key",
  "gemini_model": "gemini-2.0-flash"
}
```

---

## 12. Gemini Integration Plan

### 12.1 Gemini Chat API

**Endpoint:** `POST https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key={API_KEY}`

**Request format:**
```json
{
  "contents": [
    {
      "role": "user",
      "parts": [{"text": "Hello, how are you?"}]
    }
  ],
  "systemInstruction": {
    "parts": [{"text": "You are a cute girlfriend bot..."}]
  },
  "generationConfig": {
    "temperature": 0.9,
    "maxOutputTokens": 256
  }
}
```

**Response format:**
```json
{
  "candidates": [{
    "content": {
      "parts": [{"text": "Hey! I'm doing great..."}],
      "role": "model"
    }
  }]
}
```

### 12.2 Gemini Vision (Multimodal)

This is where Gemini shines — sending camera images along with text for multimodal understanding.

**Request format (with image):**
```json
{
  "contents": [{
    "parts": [
      {"text": "What do you see?"},
      {
        "inline_data": {
          "mime_type": "image/jpeg",
          "data": "<base64-encoded-image>"
        }
      }
    ]
  }]
}
```

**Integration with ESP32-S3 CAM:**
```
Camera capture → JPEG buffer (PSRAM) → Base64 encode → Gemini Vision API
```

**Key consideration:** ESP32-S3 CAM can capture JPEG directly in hardware, which is perfect for sending to Gemini. Image size should be kept small (320x240 or smaller) to reduce:
- Base64 encoding time
- HTTPS payload size
- API processing time

### 12.3 Gemini Visual Context Awareness

Beyond one-off "what do you see?" queries, Gemini Vision enables **continuous visual context awareness** — the bot can perceive its surroundings and factor what it sees into conversations.

#### How It Works

```
┌─────────────────────────────────────────────────────────────────┐
│                  Visual Context Pipeline                        │
│                                                                 │
│  ┌──────────┐    ┌──────────────┐    ┌───────────────────────┐ │
│  │ ESP32-S3 │───▶│ JPEG Capture │───▶│ Gemini Vision API     │ │
│  │ Camera   │    │ (PSRAM buf)  │    │ "Describe what you    │ │
│  └──────────┘    └──────────────┘    │  see briefly"         │ │
│                                      └───────────┬───────────┘ │
│                                                  │             │
│                                      ┌───────────▼───────────┐ │
│                                      │ Visual Context String │ │
│                                      │ "I can see a person   │ │
│                                      │  smiling, sitting at  │ │
│                                      │  a desk with a laptop"│ │
│                                      └───────────┬───────────┘ │
│                                                  │             │
│                          Injected into LLM system prompt       │
│                                                  │             │
│                                      ┌───────────▼───────────┐ │
│                                      │ LLM (Gemini/OpenAI)   │ │
│                                      │ System: "You are...   │ │
│                                      │ [Visual context:       │ │
│                                      │  I see a person...]"  │ │
│                                      └───────────────────────┘ │
└─────────────────────────────────────────────────────────────────┘
```

#### Context Refresh Strategy

The bot doesn't need to send an image with every conversation turn. Instead:

| Strategy | Trigger | Use Case |
|----------|---------|----------|
| **Periodic** | Every N seconds (e.g., 30s) | Ambient awareness while idle |
| **On conversation start** | When user presses talk button | Know who/what is in front before responding |
| **On keyword trigger** | User says "look at this" / "what do you see" | Explicit vision request |
| **On face change** | CV detects a new/different face | React to new person entering view |

#### Implementation: Visual Context Manager

```cpp
class VisualContextManager {
public:
    VisualContextManager(const char* geminiApiKey);

    // Capture + send to Gemini, returns description string
    String captureAndDescribe(const String& prompt = "Briefly describe what you see");

    // Get cached context (doesn't call API)
    String getCachedContext() const { return _lastContext; }

    // Check if context is stale
    bool isContextStale(unsigned long maxAgeMs = 30000) const;

    // Update context in background (non-blocking, stores result)
    void refreshContextAsync();

private:
    String _geminiApiKey;
    String _lastContext;           // Cached visual description
    unsigned long _lastUpdateTime; // When context was last refreshed
    camera_fb_t* _captureFrame(); // Grab JPEG from camera
    String _sendToGeminiVision(const uint8_t* jpegData, size_t jpegSize,
                               const String& prompt);
};
```

#### Injecting Visual Context into Conversations

The visual context becomes part of the system prompt sent to the LLM:

```cpp
String buildContextAwarePrompt(String basePrompt, String visualContext) {
    String prompt = basePrompt;
    if (visualContext.length() > 0) {
        prompt += "\n\n[Current visual context: " + visualContext + "]";
        prompt += "\nYou can reference what you see naturally in conversation.";
        prompt += " Don't mention you're using a camera — just act like you can see.";
    }
    return prompt;
}

// Usage in conversation flow:
String visualCtx = visualContextMgr.getCachedContext();
String fullPrompt = buildContextAwarePrompt(system_prompt, visualCtx);
gptChat->setSystemPrompt(fullPrompt.c_str());
String response = gptChat->sendMessage(userText);
```

#### Example Conversation with Visual Context

```
[Visual context cached: "A young person wearing a red hoodie, sitting at a desk
with books and a laptop. They appear to be studying. Room is well-lit."]

User: "Hey, how's it going?"
Bot:  "Hey! Looks like you're deep in study mode — need a break?
      Want me to quiz you on something or just chat for a bit?"

[User holds up a coffee mug]
[Visual context refreshed: "Person holding up a white coffee mug toward the camera,
smiling. Same desk setup with books."]

User: "Look what I got!"
Bot:  "Ooh, coffee time! Good call — you've been at it for a while.
      That's your third cup though, isn't it? Maybe switch to water soon!"
```

#### Camera Resource Sharing

The camera is shared between:
1. **CV face tracking** (Tomo/Kenneth's code) — continuous, low-res
2. **Gemini Vision queries** — on-demand, higher-res

Coordination approach:
```cpp
// Mutex for camera access
SemaphoreHandle_t cameraMutex = xSemaphoreCreateMutex();

// Face tracking (runs continuously on a timer/task)
void faceTrackingLoop() {
    if (xSemaphoreTake(cameraMutex, pdMS_TO_TICKS(10))) { // Short timeout
        // Capture low-res frame for face detection
        // Update servo positions
        xSemaphoreGive(cameraMutex);
    }
    // If can't get mutex, skip this frame (vision query in progress)
}

// Vision query (triggered on-demand)
String captureForGemini() {
    xSemaphoreTake(cameraMutex, portMAX_DELAY); // Wait as long as needed
    // Switch to higher resolution if needed
    // Capture JPEG
    // Switch back to low-res for face tracking
    xSemaphoreGive(cameraMutex);
    // Send to Gemini API
}
```

#### Config Addition for Visual Context

```json
{
  "vision_enabled": true,
  "vision_refresh_interval": 30000,
  "vision_on_conversation_start": true,
  "vision_resolution": "320x240"
}
```

### 12.4 Gemini Memory Format

Gemini uses a different message format than OpenAI:

```json
{
  "contents": [
    {"role": "user", "parts": [{"text": "msg 1"}]},
    {"role": "model", "parts": [{"text": "response 1"}]},
    {"role": "user", "parts": [{"text": "msg 2"}]},
    {"role": "model", "parts": [{"text": "response 2"}]},
    {"role": "user", "parts": [{"text": "current message"}]}
  ]
}
```

Note: Gemini uses `"model"` as the assistant role (not `"assistant"` like OpenAI).

### 12.5 Gemini Vision + Memory Combined

Vision context can also be stored in memory for richer recall:

```json
{
  "contents": [
    {
      "role": "user",
      "parts": [
        {"text": "What do you see?"},
        {"inline_data": {"mime_type": "image/jpeg", "data": "..."}}
      ]
    },
    {
      "role": "model",
      "parts": [{"text": "I see you sitting at your desk studying..."}]
    },
    {
      "role": "user",
      "parts": [{"text": "What was I doing earlier?"}]
    }
  ]
}
```

For long-term memory (Supabase), visual descriptions are stored as text alongside conversations:

```sql
-- conversations table extended
ALTER TABLE conversations ADD COLUMN visual_context TEXT;
-- Stores the Gemini Vision description at the time of the conversation
```

This lets the bot recall past visual contexts: *"Last time I saw you, you were wearing that red hoodie and studying!"*

---

## 13. Computer Vision & Face Tracking

> **Owner:** Tomo & Kenneth
> **Status:** Being implemented separately

### 13.1 Overview

The ESP32-S3 CAM's camera module is used for real-time face detection and tracking. Two SG90 micro servos (pan/tilt) move the robot's head to follow the detected face.

### 13.2 Architecture

```
┌───────────┐     ┌────────────────┐     ┌──────────────┐     ┌────────────┐
│ ESP32-S3  │────▶│ Face Detection │────▶│ Position     │────▶│ SG90 Servos│
│ Camera    │     │ Algorithm      │     │ Calculation  │     │ Pan + Tilt │
│ (OV2640)  │     │ (on-device)    │     │ (PID/simple) │     │            │
└───────────┘     └────────────────┘     └──────────────┘     └────────────┘
```

### 13.3 Face Detection Approach

**On-device options:**
- **ESP-WHO framework** — Espressif's official face detection library for ESP32-S3
- **Simple color/blob tracking** — Lighter, faster, less accurate
- **Haar cascades** — Classic approach, moderate resource usage

**Off-device option (if needed):**
- Capture frame → send to cloud API (Google Vision, Gemini) → get face coordinates
- Higher latency (~1-2s) but more accurate

### 13.4 Servo Control

| Servo | Axis | Range | Center | Purpose |
|-------|------|-------|--------|---------|
| SG90 #1 | X (Pan) | 0-180 degrees | 90 degrees | Left/right tracking |
| SG90 #2 | Y (Tilt) | 0-180 degrees | 90 degrees | Up/down tracking |

**Basic tracking logic:**
```
face_center_x = detected_face.x + detected_face.width / 2
face_center_y = detected_face.y + detected_face.height / 2

error_x = frame_center_x - face_center_x
error_y = frame_center_y - face_center_y

servo_pan  += error_x * Kp   // Proportional control
servo_tilt += error_y * Kp
```

### 13.5 Integration Points with Main System

The CV/servo code needs to coordinate with the rest of the system:

| Integration Point | How | Notes |
|------------------|-----|-------|
| **Camera sharing** | Mutex/semaphore | CV pauses when Gemini Vision captures |
| **State awareness** | Check `currentState` | Maybe stop tracking during TTS playback to reduce servo noise |
| **Face events** | Callback to main loop | Notify when new face detected (trigger greeting) |
| **GPIO coordination** | Pin allocation | Servo PWM pins must not conflict with I2S/I2C |

### 13.6 Potential Enhancements (Stretch Goals)

- **Face recognition** — Identify specific people, personalize greetings
- **Emotion detection** — Detect smiles/frowns, adjust bot personality
- **Gesture tracking** — Wave to wake, hand signals for commands
- **Multi-face handling** — Track primary face, acknowledge others

### 13.7 Libraries Tomo/Kenneth May Use

| Library | Purpose |
|---------|---------|
| ESP32Servo | PWM servo control |
| esp_camera | Camera driver |
| ESP-WHO / ESP-DL | Face detection models |
| fb_gfx | Frame buffer graphics (debug drawing) |

---

## 14. Supabase Memory Architecture

### 14.1 Three-Layer Memory Design

The bot's memory has three distinct layers for different recall needs:

```
┌──────────────────────────────────────────────────────────────────┐
│                       Memory Architecture                        │
│                                                                  │
│  ┌──────────────────────┐                                        │
│  │  Layer 1: Short-Term │ ◀── In-RAM on ESP32                    │
│  │  Last 5 exchanges    │     Instant access, lost on reboot     │
│  │  ~2KB                │     Used in every LLM call             │
│  └──────────┬───────────┘                                        │
│             │ every exchange stored async                         │
│             ▼                                                    │
│  ┌──────────────────────┐                                        │
│  │  Layer 2: Long-Term  │ ◀── Supabase (pgvector)                │
│  │  Conversations       │     Persists across reboots            │
│  │  + Visual Events     │     Semantic search via embeddings     │
│  │  + Topic Summaries   │     "What did we talk about last week?"│
│  └──────────┬───────────┘                                        │
│             │ auto-generated by Edge Function                     │
│             ▼                                                    │
│  ┌──────────────────────┐                                        │
│  │  Layer 3: Embeddings │ ◀── Vector similarity search           │
│  │  (pgvector)          │     "Find memories relevant to X"      │
│  │  Semantic recall     │     Powers contextual recall            │
│  └──────────────────────┘                                        │
└──────────────────────────────────────────────────────────────────┘
```

**Why vector search?** Chronological retrieval ("last 10 messages") misses relevant context. If the user mentioned their dog's name 3 days ago, a simple `ORDER BY created_at DESC LIMIT 10` won't find it. Vector similarity search finds memories by **meaning**, not recency — so when the user says "How's my dog?", the bot recalls the dog's name from 3 days ago.

### 14.2 Memory Types

The bot stores three kinds of memories:

| Type | What It Stores | Example |
|------|---------------|---------|
| **Conversations** | What was said (user + bot messages) | "User asked about cooking pasta, bot gave a recipe" |
| **Visual Events** | What the bot saw via camera/Gemini Vision | "Saw user wearing red hoodie, studying at desk" |
| **Topic Summaries** | Condensed themes from multiple conversations | "User is interested in cooking, studying CS, has a dog named Max" |

### 14.3 Supabase Schema (pgvector + Auto-Embeddings)

Supabase has **pgvector** built in for vector similarity search, plus **Edge Functions** that auto-generate embeddings. The ESP32 never needs to compute embeddings — it just sends text, and Supabase handles the rest.

```sql
-- Enable required extensions
CREATE EXTENSION IF NOT EXISTS vector WITH SCHEMA extensions;
CREATE EXTENSION IF NOT EXISTS pgmq;
CREATE EXTENSION IF NOT EXISTS pg_net WITH SCHEMA extensions;
CREATE EXTENSION IF NOT EXISTS pg_cron;

-- ============================================================
-- Table 1: Conversations (what was spoken)
-- ============================================================
CREATE TABLE conversations (
  id INTEGER PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
  device_id TEXT NOT NULL,                          -- ESP32 MAC address
  user_message TEXT NOT NULL,
  assistant_message TEXT NOT NULL,
  ai_provider TEXT DEFAULT 'gemini',                -- 'openai' or 'gemini'
  visual_context TEXT,                              -- what bot saw during this exchange
  embedding HALFVEC(1536),                          -- auto-generated vector
  created_at TIMESTAMPTZ DEFAULT NOW()
);

-- Vector index for semantic search
CREATE INDEX ON conversations USING hnsw (embedding halfvec_cosine_ops);
-- Chronological index for recent lookups
CREATE INDEX idx_conversations_device ON conversations(device_id, created_at DESC);

-- ============================================================
-- Table 2: Visual Events (what the bot witnessed)
-- ============================================================
CREATE TABLE visual_events (
  id INTEGER PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
  device_id TEXT NOT NULL,
  description TEXT NOT NULL,                        -- Gemini Vision description
  event_type TEXT DEFAULT 'observation',            -- 'observation', 'face_detected', 'scene_change'
  embedding HALFVEC(1536),                          -- auto-generated vector
  created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX ON visual_events USING hnsw (embedding halfvec_cosine_ops);
CREATE INDEX idx_visual_events_device ON visual_events(device_id, created_at DESC);

-- ============================================================
-- Table 3: Topic Summaries (condensed long-term knowledge)
-- ============================================================
CREATE TABLE topic_summaries (
  id INTEGER PRIMARY KEY GENERATED ALWAYS AS IDENTITY,
  device_id TEXT NOT NULL,
  topic TEXT NOT NULL,                              -- e.g. "user's pets", "cooking interests"
  summary TEXT NOT NULL,                            -- condensed knowledge
  last_referenced TIMESTAMPTZ DEFAULT NOW(),        -- for relevance decay
  embedding HALFVEC(1536),                          -- auto-generated vector
  created_at TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX ON topic_summaries USING hnsw (embedding halfvec_cosine_ops);

-- ============================================================
-- Row Level Security
-- ============================================================
ALTER TABLE conversations ENABLE ROW LEVEL SECURITY;
ALTER TABLE visual_events ENABLE ROW LEVEL SECURITY;
ALTER TABLE topic_summaries ENABLE ROW LEVEL SECURITY;

CREATE POLICY "device_access" ON conversations
  FOR ALL USING (device_id = current_setting('request.header.x-device-id', true));
CREATE POLICY "device_access" ON visual_events
  FOR ALL USING (device_id = current_setting('request.header.x-device-id', true));
CREATE POLICY "device_access" ON topic_summaries
  FOR ALL USING (device_id = current_setting('request.header.x-device-id', true));
```

### 14.4 Auto-Embedding Pipeline (Serverless)

The ESP32 just stores plain text. Supabase **automatically generates embeddings** using Edge Functions + pgmq queue — no compute needed on the ESP32, no PC in the loop.

```
ESP32 stores conversation ──POST──▶ Supabase REST API
                                         │
                                    (row inserted)
                                         │
                                    DB trigger fires
                                         │
                                    pgmq queue job
                                         │
                                    pg_cron (every 10s)
                                         │
                                    Edge Function "embed"
                                         │
                                    Generates embedding
                                    (OpenAI text-embedding-3-small
                                     or Supabase built-in gte-small)
                                         │
                                    Updates row with vector
                                         │
                                    Ready for similarity search
```

**Embedding function for conversations:**
```sql
-- Combines user message + assistant response + visual context into
-- a single string for embedding generation
CREATE OR REPLACE FUNCTION conversation_embedding_input(row conversations)
RETURNS TEXT
LANGUAGE plpgsql IMMUTABLE
AS $$
BEGIN
  RETURN 'User said: ' || row.user_message
    || E'\nBot replied: ' || row.assistant_message
    || COALESCE(E'\nVisual context: ' || row.visual_context, '');
END;
$$;

-- Trigger: auto-queue embedding on insert
CREATE TRIGGER embed_conversations_on_insert
  AFTER INSERT ON conversations
  FOR EACH ROW
  EXECUTE FUNCTION util.queue_embeddings('conversation_embedding_input', 'embedding');
```

**Embedding function for visual events:**
```sql
CREATE OR REPLACE FUNCTION visual_event_embedding_input(row visual_events)
RETURNS TEXT
LANGUAGE plpgsql IMMUTABLE
AS $$
BEGIN
  RETURN row.event_type || ': ' || row.description;
END;
$$;

CREATE TRIGGER embed_visual_events_on_insert
  AFTER INSERT ON visual_events
  FOR EACH ROW
  EXECUTE FUNCTION util.queue_embeddings('visual_event_embedding_input', 'embedding');
```

### 14.5 Semantic Search Functions

These Postgres functions let the ESP32 retrieve **relevant** memories, not just recent ones.

```sql
-- Find conversations similar to a query
CREATE OR REPLACE FUNCTION match_conversations(
  query_embedding EXTENSIONS.HALFVEC(1536),
  target_device_id TEXT,
  match_threshold FLOAT DEFAULT 0.7,
  match_count INT DEFAULT 5
)
RETURNS TABLE (
  id INT,
  user_message TEXT,
  assistant_message TEXT,
  visual_context TEXT,
  similarity FLOAT,
  created_at TIMESTAMPTZ
)
LANGUAGE sql AS $$
  SELECT
    c.id,
    c.user_message,
    c.assistant_message,
    c.visual_context,
    1 - (c.embedding <=> query_embedding) AS similarity,
    c.created_at
  FROM conversations c
  WHERE c.device_id = target_device_id
    AND c.embedding IS NOT NULL
    AND (c.embedding <=> query_embedding) < (1 - match_threshold)
  ORDER BY c.embedding <=> query_embedding ASC
  LIMIT match_count;
$$;

-- Find visual events similar to a query
CREATE OR REPLACE FUNCTION match_visual_events(
  query_embedding EXTENSIONS.HALFVEC(1536),
  target_device_id TEXT,
  match_threshold FLOAT DEFAULT 0.7,
  match_count INT DEFAULT 3
)
RETURNS TABLE (
  id INT,
  description TEXT,
  event_type TEXT,
  similarity FLOAT,
  created_at TIMESTAMPTZ
)
LANGUAGE sql AS $$
  SELECT
    v.id,
    v.description,
    v.event_type,
    1 - (v.embedding <=> query_embedding) AS similarity,
    v.created_at
  FROM visual_events v
  WHERE v.device_id = target_device_id
    AND v.embedding IS NOT NULL
    AND (v.embedding <=> query_embedding) < (1 - match_threshold)
  ORDER BY v.embedding <=> query_embedding ASC
  LIMIT match_count;
$$;
```

### 14.6 Retrieval Edge Function (ESP32 Calls This)

Since the ESP32 **can't generate embeddings locally**, we use a Supabase Edge Function as the retrieval endpoint. The ESP32 sends a text query, the Edge Function generates the embedding and runs the similarity search.

```typescript
// supabase/functions/recall/index.ts
import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import OpenAI from "jsr:@openai/openai";
import postgres from "https://deno.land/x/postgresjs@v3.4.5/mod.js";

const openai = new OpenAI({ apiKey: Deno.env.get("OPENAI_API_KEY") });
const sql = postgres(Deno.env.get("SUPABASE_DB_URL")!);

Deno.serve(async (req) => {
  const { query, device_id } = await req.json();

  // Generate embedding for the query
  const response = await openai.embeddings.create({
    model: "text-embedding-3-small",
    input: query,
  });
  const embedding = response.data[0].embedding;

  // Search conversations + visual events in parallel
  const [conversations, visualEvents] = await Promise.all([
    sql`SELECT * FROM match_conversations(
      ${JSON.stringify(embedding)}::halfvec(1536),
      ${device_id}, 0.7, 5
    )`,
    sql`SELECT * FROM match_visual_events(
      ${JSON.stringify(embedding)}::halfvec(1536),
      ${device_id}, 0.7, 3
    )`,
  ]);

  return new Response(JSON.stringify({ conversations, visualEvents }), {
    headers: { "Content-Type": "application/json" },
  });
});
```

### 14.7 ESP32 → Supabase Communication

**Store a conversation (plain REST — ESP32 does this):**
```
POST https://<project>.supabase.co/rest/v1/conversations
Headers:
  apikey: <supabase-anon-key>
  Content-Type: application/json
  Prefer: return=minimal

Body:
{
  "device_id": "AA:BB:CC:DD:EE:FF",
  "user_message": "How's my dog doing?",
  "assistant_message": "I bet Max is happy to see you!",
  "ai_provider": "gemini",
  "visual_context": "User sitting at desk, smiling"
}
```

**Store a visual event:**
```
POST https://<project>.supabase.co/rest/v1/visual_events
Headers:
  apikey: <supabase-anon-key>
  Content-Type: application/json
  Prefer: return=minimal

Body:
{
  "device_id": "AA:BB:CC:DD:EE:FF",
  "description": "New person entered the room. They are wearing a blue jacket and waved at the camera.",
  "event_type": "face_detected"
}
```

**Retrieve relevant memories (calls Edge Function):**
```
POST https://<project>.supabase.co/functions/v1/recall
Headers:
  Authorization: Bearer <supabase-anon-key>
  Content-Type: application/json

Body:
{
  "query": "How's my dog doing?",
  "device_id": "AA:BB:CC:DD:EE:FF"
}

Response:
{
  "conversations": [
    {
      "user_message": "My dog Max learned a new trick today!",
      "assistant_message": "That's amazing! What trick did Max learn?",
      "visual_context": "User showing phone screen with dog photo",
      "similarity": 0.89,
      "created_at": "2026-02-10T14:30:00Z"
    }
  ],
  "visualEvents": [
    {
      "description": "Small brown dog visible in background, running around room",
      "event_type": "observation",
      "similarity": 0.82,
      "created_at": "2026-02-12T09:15:00Z"
    }
  ]
}
```

### 14.8 Full Memory Retrieval Flow

```
1. User speaks → ASR → "How's my dog doing?"
2. ESP32 calls /recall Edge Function with query text
3. Edge Function generates embedding for query
4. pgvector similarity search across:
   a. conversations table → finds "Max learned a new trick" (3 days ago)
   b. visual_events table → finds "brown dog running in room" (2 days ago)
5. Returns relevant memories to ESP32
6. ESP32 builds LLM prompt:
   ┌─────────────────────────────────────────────────────┐
   │ System: "You are a girlfriend bot..."               │
   │                                                     │
   │ [Recalled memories:                                 │
   │  - 3 days ago, user's dog Max learned a new trick   │
   │  - 2 days ago, I saw a brown dog running in room    │
   │  - Currently: user sitting at desk, smiling]        │
   │                                                     │
   │ [Recent conversation: last 5 exchanges from RAM]    │
   │                                                     │
   │ User: "How's my dog doing?"                         │
   └─────────────────────────────────────────────────────┘
7. LLM responds: "I hope Max is doing well! Has he
   been practicing that new trick? Last time I saw
   him he was zooming around the room!"
8. Store exchange in short-term (RAM) + async to Supabase
```

### 14.9 Topic Summary Generation (Stretch Goal)

Over time, raw conversations pile up. A scheduled Edge Function can **condense** recurring topics into summaries:

```typescript
// Runs weekly via pg_cron
// Groups conversations by topic clusters, asks LLM to summarize
// Stores in topic_summaries table

// Example output:
// topic: "user's pets"
// summary: "User has a dog named Max (brown, small breed). Max recently
//           learned a new trick. User frequently mentions Max and seems
//           very attached."
```

This keeps the memory efficient — instead of searching through thousands of old conversations, the bot can first check topic summaries for quick recall.

### 14.10 Config Addition

```json
{
  "supabase_url": "https://yourproject.supabase.co",
  "supabase_anon_key": "eyJ...",
  "memory_mode": "both",
  "memory_store_visual_events": true,
  "memory_visual_event_interval": 60000
}
```

| Option | Values | Description |
|--------|--------|-------------|
| `memory_mode` | `"local"`, `"supabase"`, `"both"` | Where to store memories |
| `memory_store_visual_events` | `true`/`false` | Log what the bot sees over time |
| `memory_visual_event_interval` | milliseconds | How often to log visual observations (default: 60s) |

### 14.11 Why This Architecture Is Serverless

```
ESP32 ──POST text──▶ Supabase REST API     (stores raw text)
                          │
                     DB trigger + pgmq      (queues embedding job)
                          │
                     Edge Function          (generates embedding, runs ON Supabase)
                          │
                     pgvector               (stores + indexes vectors)

ESP32 ──POST query──▶ Edge Function /recall (generates query embedding + searches)
                          │
                     Returns relevant memories
```

**No PC, no server, no Lambda.** The ESP32 sends plain HTTPS requests. All embedding generation, vector storage, and similarity search happens inside Supabase's infrastructure.

---

## 15. Web App Control Interface

### 15.1 Architecture

The ESP32 hosts a lightweight web server. Users connect via the ESP32's IP on the local WiFi network.

```
┌──────────────┐       WiFi        ┌──────────────────┐
│  Phone/PC    │ ◀──────────────▶  │    ESP32 Web     │
│  Browser     │   HTTP + WS       │    Server        │
│  (Web App)   │                   │  (Port 80)       │
└──────────────┘                   └──────────────────┘
```

### 15.2 ESP32 Web Server Setup

Using `ESPAsyncWebServer` (non-blocking, handles requests during audio processing):

```cpp
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");  // WebSocket for real-time updates

void setupWebServer() {
    // Serve web app from SPIFFS
    server.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");

    // REST API endpoints
    server.on("/api/status", HTTP_GET, handleGetStatus);
    server.on("/api/config", HTTP_GET, handleGetConfig);
    server.on("/api/config", HTTP_POST, handleSetConfig);
    server.on("/api/start", HTTP_POST, handleStartConversation);
    server.on("/api/stop", HTTP_POST, handleStopConversation);
    server.on("/api/prompt", HTTP_POST, handleSetPrompt);
    server.on("/api/model", HTTP_POST, handleSelectModel);
    server.on("/api/memory", HTTP_GET, handleGetMemory);
    server.on("/api/memory", HTTP_DELETE, handleClearMemory);

    // WebSocket for real-time updates
    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);

    server.begin();
}
```

### 15.3 REST API Endpoints

| Method | Path | Purpose |
|--------|------|---------|
| `GET` | `/api/status` | System status (state, memory usage, WiFi signal) |
| `GET` | `/api/config` | Current configuration |
| `POST` | `/api/config` | Update configuration (same JSON format as serial) |
| `POST` | `/api/start` | Start conversation mode |
| `POST` | `/api/stop` | Stop conversation mode |
| `POST` | `/api/prompt` | Update system prompt |
| `POST` | `/api/model` | Switch AI model (`{"provider": "gemini"}`) |
| `GET` | `/api/memory` | View conversation history |
| `DELETE` | `/api/memory` | Clear conversation history |

### 15.4 WebSocket Events (Real-Time)

```json
// Server → Client events
{"event": "state_change", "state": "listening"}
{"event": "asr_result", "text": "Hello there"}
{"event": "llm_response", "text": "Hi! How are you?"}
{"event": "tts_started"}
{"event": "tts_completed"}
{"event": "error", "message": "WiFi disconnected"}
```

### 15.5 Web App Features

- **Dashboard:** Current state, memory usage, API status
- **Config panel:** WiFi, API keys, model selection, voice settings
- **Conversation view:** Live transcript of all exchanges
- **Prompt editor:** Modify personality/system prompt
- **Model switcher:** Toggle between OpenAI and Gemini
- **Memory viewer:** Browse and clear conversation history

### 15.6 Dependencies

```
ESPAsyncWebServer  (https://github.com/me-no-dev/ESPAsyncWebServer)
AsyncTCP           (https://github.com/me-no-dev/AsyncTCP)
```

**Flash storage note:** Web app HTML/CSS/JS stored in SPIFFS. Must account for this in the partition scheme (current: 3MB APP / 1.5MB SPIFFS — should be sufficient).

---

## 16. Serverless Architecture

### Current Status: Already Serverless

The DAZI-AI library is **already fully serverless** — the ESP32 communicates directly with cloud APIs. No intermediate server (PC, Raspberry Pi, etc.) is needed.

```
ESP32 ──HTTPS──▶ ByteDance ASR      (direct)
ESP32 ──HTTPS──▶ OpenAI ChatGPT     (direct)
ESP32 ──HTTPS──▶ OpenAI TTS         (direct)
ESP32 ──WSS───▶ MiniMax TTS         (direct)
```

### What We Add (Still Serverless)

```
ESP32 ──HTTPS──▶ Google Gemini API   (direct, new)
ESP32 ──HTTPS──▶ Supabase REST API   (direct, new)
ESP32 ──WiFi──▶ Local Web App        (hosted on ESP32 itself)
```

### Key Principle

**No personal computer in the loop.** Every API call is ESP32 → Cloud Service directly via WiFi. The web app is hosted ON the ESP32 and accessed by any device on the same WiFi network.

### Latency Considerations

| Operation | Expected Latency | Notes |
|-----------|-----------------|-------|
| ASR (WebSocket) | 300-2000ms | Depends on speech length |
| Gemini Chat | 500-3000ms | Lower for Flash models |
| Gemini Vision | 1000-5000ms | Image upload + processing |
| OpenAI Chat | 500-5000ms | Variable |
| TTS Streaming | ~100ms start | MiniMax WebSocket |
| Supabase Write | 100-500ms | Async, non-blocking |
| Supabase Read | 100-300ms | At conversation start |

### SSL Consideration

All cloud APIs require HTTPS. Currently `setInsecure()` is used (skips cert verification). For production, consider:
- Using `setCACertBundle()` with the ESP32's built-in CA certificate bundle
- Or pinning specific certificates for known APIs

---

## 17. Dependencies

### Current (from library.properties)

| Library | Version | Purpose |
|---------|---------|---------|
| ArduinoWebsocket | >= 0.5.4 | WebSocket client for ASR/TTS |
| ArduinoJson | >= 7.4.1 | JSON parsing |
| Seeed_Arduino_mbedtls | >= 3.0.2 | SHA1/Base64 for WebSocket handshake |

### Additional Required

| Library | Purpose |
|---------|---------|
| ESPAsyncWebServer | Web server for control app |
| AsyncTCP | Async TCP for web server |
| ESP32Servo | Servo control for head movement |
| Adafruit_SSD1306 (or U8g2) | OLED display for face |

### ESP32 Board Settings

```
Board: ESP32S3 Dev Module
CPU Frequency: 240MHz (WiFi)
Flash Size: 8MB (64Mb)
Partition Scheme: 8M with spiffs (3MB APP / 1.5MB SPIFFS)
PSRAM: OPI PSRAM
Upload Speed: 921600
```

---

## 18. Recommended Starting Example

**Use `examples/chat_configurable/chat_configurable.ino` as the base.**

### Why:
1. JSON configuration via serial (no hardcoded keys)
2. Flash persistence (survives reboots)
3. Free/Pro TTS switching (pattern for AI model switching)
4. Clean state machine (easy to extend)
5. Dynamic object allocation (can add providers at runtime)

### Modification Priority for Hackathon

| Order | Task | Who | Difficulty |
|-------|------|-----|-----------|
| 1 | Get `chat_configurable` running on ESP32-S3 CAM | Sho | Low |
| 2 | Add Gemini provider (chat only) | Sho | Medium |
| 3 | Add Gemini Vision + visual context awareness | Sho | Medium |
| 4 | CV face tracking + servo control | Tomo + Kenneth | Medium |
| 5 | Integrate CV with Gemini Vision (camera sharing) | Sho + Tomo | Medium |
| 6 | Supabase memory integration | Sho | Medium |
| 7 | Web app control interface | Sho | High |
| 8 | OLED face animations | Kenneth | Medium |
| 9 | AI model switching via config/webapp | Sho | Low |

### Quick-Start Gemini Test

Before modifying the library, you can test Gemini from ESP32 with a simple HTTP POST:

```cpp
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

String callGemini(String prompt, String apiKey) {
    WiFiClientSecure client;
    client.setInsecure(); // TODO: add proper cert

    String url = "/v1beta/models/gemini-2.0-flash:generateContent?key=" + apiKey;

    JsonDocument doc;
    doc["contents"][0]["parts"][0]["text"] = prompt;
    String payload;
    serializeJson(doc, payload);

    client.connect("generativelanguage.googleapis.com", 443);
    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: generativelanguage.googleapis.com");
    client.println("Content-Type: application/json");
    client.println("Content-Length: " + String(payload.length()));
    client.println("Connection: close");
    client.println();
    client.print(payload);

    // Read response...
    // Parse: candidates[0].content.parts[0].text
}
```

---

## Appendix A: File-Level Modification Map

| File | Modification | Purpose |
|------|-------------|---------|
| `src/ArduinoGPTChat.h` | Extract AIProvider interface | Abstraction layer |
| `src/ArduinoGPTChat.cpp:562-591` | Refactor sendMessage | Provider-agnostic |
| `src/ArduinoGPTChat.cpp:609` | Remove hardcoded model | Configurable |
| **NEW** `src/GeminiProvider.h/cpp` | Gemini chat + vision | New provider |
| **NEW** `src/VisualContextManager.h/cpp` | Camera → Gemini Vision → context string | Visual awareness |
| **NEW** `src/SupabaseMemory.h/cpp` | Long-term memory | Persistence |
| **NEW** `src/WebControl.h/cpp` | Web server + API | Control interface |
| `examples/chat_configurable.ino` | Add provider selection | Entry point |
| `examples/chat_configurable.ino:447` | Init Gemini or OpenAI | Runtime switch |
| `examples/chat_configurable.ino:288-373` | Extend JSON config | New fields |

## Appendix B: Gemini vs OpenAI API Comparison

| Feature | OpenAI | Gemini |
|---------|--------|--------|
| Chat endpoint | `/v1/chat/completions` | `/v1beta/models/{model}:generateContent` |
| Auth | Bearer token in header | API key in URL query param |
| Message format | `{"role": "user", "content": "..."}` | `{"role": "user", "parts": [{"text": "..."}]}` |
| Assistant role name | `"assistant"` | `"model"` |
| System prompt | `{"role": "system", "content": "..."}` | `"systemInstruction": {"parts": [{"text": "..."}]}` |
| Vision | Separate model (GPT-4V) | Same model, add `inline_data` part |
| Streaming | SSE | SSE |
| Free tier | No | Yes (rate limited) |

## Appendix C: Supabase Setup Checklist

1. Create Supabase project at [supabase.com](https://supabase.com)
2. Run the SQL from Section 13.2 in the SQL editor
3. Copy the project URL and anon key
4. Add to ESP32 JSON config
5. Test with a simple POST from ESP32
6. Enable RLS policies for security
