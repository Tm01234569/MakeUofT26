# ElevenLabs TTS Setup

This firmware uses ElevenLabs Text-to-Speech in free mode when `use_elevenlabs_tts=true`.

## Required JSON Fields

```json
{"use_elevenlabs_tts":true,"elevenlabs_api_key":"YOUR_KEY","elevenlabs_voice_id":"EST9Ui6982FZPSi7gCHi","elevenlabs_model_id":"eleven_flash_v2_5","elevenlabs_output_format":"mp3_22050_32"}
```

## Notes

- `elevenlabs_voice_id`: your voice selection ID.
- `elevenlabs_model_id`: model used for synthesis (default in firmware: `eleven_flash_v2_5`).
- `elevenlabs_output_format`: mp3 output profile.

## API Endpoint Used By Firmware

`POST https://api.elevenlabs.io/v1/text-to-speech/{voice_id}?output_format=...`

Headers used:
- `xi-api-key`
- `Content-Type: application/json`
- `Accept: audio/mpeg`

## Official Docs

- ElevenLabs API reference: https://elevenlabs.io/docs/api-reference/overview
- Text to Speech endpoint: https://elevenlabs.io/docs/api-reference/text-to-speech
