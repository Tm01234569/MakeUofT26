# Implementation Notes (Current Stack)

Current production path in `MakeUofT26`:

- ASR: ByteDance/Volcengine (`ArduinoASRChat`)
- LLM + vision: Gemini (`GeminiProvider`)
- TTS: ElevenLabs (`ElevenLabsTTS`)
- Memory backend: MongoDB Atlas via `backend/memory-api` + `RemoteMemory`
- Device control: `WebControl` API + serial runtime commands

## `chat_configurable` capabilities

- Provider select: `gemini` (recommended) or `openai`
- ElevenLabs TTS in free mode (`use_elevenlabs_tts=true`)
- Visual context policy:
  - on conversation start
  - on user turn
  - periodic while active
  - dedupe + rate limiting for memory writes
- Remote memory mode: `local|remote|both`
- Web control status includes vision diagnostics

## JSON keys (recommended baseline)

```json
{
  "wifi_ssid": "...",
  "wifi_password": "...",
  "subscription": "free",
  "asr_api_key": "...",
  "asr_cluster": "volcengine_input_en",
  "ai_provider": "gemini",
  "gemini_apiKey": "...",
  "gemini_model": "gemini-2.0-flash",
  "use_elevenlabs_tts": true,
  "elevenlabs_api_key": "...",
  "elevenlabs_voice_id": "EST9Ui6982FZPSi7gCHi",
  "elevenlabs_model_id": "eleven_flash_v2_5",
  "elevenlabs_output_format": "mp3_22050_32",
  "memory_api_url": "https://your-memory-api.example.com",
  "memory_api_key": "...",
  "memory_mode": "both",
  "vision_enabled": true,
  "vision_capture_on_user_turn": true,
  "vision_refresh_interval": 60000,
  "vision_dedupe_threshold_pct": 90,
  "vision_min_store_interval_ms": 30000,
  "vision_max_events_per_hour": 30,
  "web_control_enabled": true
}
```

## Backend contract

`backend/memory-api` exposes:

- `POST /v1/memory/conversations`
- `POST /v1/memory/visual-events`
- `POST /v1/memory/recall`

Atlas vector recall is used when embeddings/index are available, with lexical fallback.

## Security notes

- Do not hardcode API keys in firmware source.
- Rotate exposed keys immediately.
- Restrict backend CORS/network access before final demo if possible.
