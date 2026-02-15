# Audio Backend Streaming Runbook (ESP32 + Laptop API)

This runbook is the stable path for ESP32 (no PSRAM):
- ESP32 streams microphone PCM chunks to laptop backend
- backend assembles/transcribes (Gemini)
- backend runs LLM (Gemini)
- backend runs TTS (ElevenLabs)
- ESP32 plays returned audio

## 1) Backend Setup

```bash
cd /Users/shoadachi/Projects/MakeUofT/MakeUofT26/backend/memory-api
npm install
cp .env.example .env
```

Set these in `.env`:
- `MEMORY_API_KEY=...`
- `GEMINI_API_KEY=...`
- `ELEVENLABS_API_KEY=...`
- `MONGODB_URI=...` (optional for audio-only, required for memory endpoints)
- `MONGODB_DB=makeuoft26`
- `MONGODB_COLLECTION=memory_events`

Start backend:

```bash
npm run dev
```

Verify:

```bash
curl http://127.0.0.1:8787/healthz
```

## 2) ESP32 Firmware Build + Upload

```bash
cd /Users/shoadachi/Projects/MakeUofT/MakeUofT26

arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --library /Users/shoadachi/Projects/MakeUofT/MakeUofT26/firmware \
  firmware/examples/chat_configurable

PORT=/dev/tty.usbserial-0001
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml upload \
  -p "$PORT" \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --upload-property upload.speed=115200 \
  firmware/examples/chat_configurable
```

## 3) Monitor + Provision Config

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml monitor \
  -p "$PORT" \
  -c baudrate=115200,dtr=off,rts=off
```

If config already exists in Flash, you can type runtime commands directly.
If provisioning from scratch, paste JSON in one line at config prompt.

Use this JSON template (replace values):

```json
{"wifi_ssid":"YOUR_HOTSPOT","wifi_password":"YOUR_PASS","subscription":"free","asr_provider":"backend","asr_api_url":"http://YOUR_LAPTOP_IP:8787","asr_api_key":"YOUR_MEMORY_API_KEY","single_turn_mode":true,"manual_record_control":true,"ai_provider":"backend","backend_api_url":"http://YOUR_LAPTOP_IP:8787","backend_api_key":"YOUR_MEMORY_API_KEY","gemini_model":"gemini-2.0-flash","use_backend_tts":true,"elevenlabs_voice_id":"EST9Ui6982FZPSi7gCHi","elevenlabs_model_id":"eleven_flash_v2_5","memory_mode":"local","vision_enabled":false}
```

## 4) Runtime Test Flow

In monitor:

```text
status
start
# speak naturally
stop
```

Expected:
1. ASR recognition printed
2. LLM response printed
3. Backend TTS playback starts

## 5) Required Networking

Laptop and ESP32 must be on the same hotspot/Wi-Fi subnet.

Get laptop IP:

```bash
ipconfig getifaddr en0
```

Verify backend reachable on that IP:

```bash
curl http://<LAPTOP_IP>:8787/healthz
```

## 6) Common Failures

- `[Backend ASR] HTTP -1`:
  - backend not running, wrong IP, different network, or firewall block.
- `LLM Provider: Gemini` at boot (instead of backend):
  - old flash config loaded. Use runtime command `provider:backend` then `status`.
- ElevenLabs `402 payment_required`:
  - selected voice/model not allowed on current ElevenLabs plan.
- `stop` says `Already stopped`:
  - session already finalized; run `start` again.

## 7) Quick Runtime Commands

- `status` -> prints active providers/urls/modes
- `diag` -> prints detailed runtime health
- `provider:backend` -> switch LLM provider to backend and save to flash
- `start` / `stop` -> manual speech toggle

