# JSON Config Reference

All config is sent over serial as **one-line JSON**.

## General Rules

1. One line only.
2. Use plain `"` quotes.
3. Press Enter once.
4. Wait for `[Config] Configuration received successfully!`.

## A) Main Runtime (chat_configurable) - Gemini LLM/Vision + ElevenLabs TTS + Mongo Memory API

```json
{"wifi_ssid":"YOUR_WIFI","wifi_password":"YOUR_PASS","subscription":"free","asr_api_key":"YOUR_ASR_KEY","asr_cluster":"volcengine_input_en","ai_provider":"gemini","gemini_apiKey":"YOUR_GEMINI_KEY","gemini_model":"gemini-2.0-flash","use_elevenlabs_tts":true,"elevenlabs_api_key":"YOUR_ELEVENLABS_KEY","elevenlabs_voice_id":"EST9Ui6982FZPSi7gCHi","elevenlabs_model_id":"eleven_flash_v2_5","elevenlabs_output_format":"mp3_22050_32","system_prompt":"You are a helpful assistant.","vision_enabled":true,"vision_on_conversation_start":true,"vision_capture_on_user_turn":true,"vision_refresh_interval":60000,"vision_dedupe_threshold_pct":90,"vision_min_store_interval_ms":30000,"vision_max_events_per_hour":30,"memory_api_url":"https://memory-api.example.com","memory_api_key":"YOUR_MEMORY_API_KEY","memory_mode":"both","web_control_enabled":true}
```

## B) Speaker Test Only (speaker_tts_test)

```json
{"wifi_ssid":"YOUR_WIFI","wifi_password":"YOUR_PASS","tts_api_key":"YOUR_OPENAI_KEY","tts_api_base_url":"https://api.openai.com","tts_model":"gpt-4o-mini-tts","tts_voice":"alloy","tts_speed":"1.0","volume":90,"speak_text":"Hello this is your personal AI assistance through the speaker.","i2s_bclk":26,"i2s_lrc":25,"i2s_dout":27}
```

## Required Fields (Main Runtime)

- `wifi_ssid`
- `wifi_password`
- `asr_api_key`
- If `ai_provider="gemini"`, then `gemini_apiKey` is required
- If `use_elevenlabs_tts=true`, then `elevenlabs_api_key` is required
- If `memory_mode` is `remote` or `both`, then `memory_api_url` and `memory_api_key` are required

## Recommended Fields

- `elevenlabs_voice_id` and `elevenlabs_model_id`
- vision policy keys for low load
- `web_control_enabled` for browser diagnostics
- `memory_mode:"both"` for local + remote memory

## Common Mistakes

- smart quotes `”` instead of `"`
- broken JSON like `"password"11`
- multiline JSON with accidental line breaks
