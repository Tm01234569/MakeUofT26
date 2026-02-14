# Wi-Fi JSON Provisioning Guide (General Purpose)

Use this guide to connect any teammate's ESP32 firmware instance to Wi‑Fi without changing code.

## Concept

The firmware accepts a JSON config over serial, validates it, and stores it in ESP32 flash (Preferences/NVS).

- No code edits required for SSID/password changes
- Re-send JSON any time to update config

## Method A: Arduino CLI

1. Upload firmware first.
2. Open monitor:

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml monitor \
  -p /dev/tty.usbserial-0001 \
  -c baudrate=115200,dtr=off,rts=off
```

3. Paste one-line JSON and press Enter.
4. Wait for `Configuration received successfully` or `[Config] OK`.
5. Press `BOOT` when firmware prompts you.

## Method B: Arduino IDE

1. Upload firmware.
2. Open `Tools -> Serial Monitor`.
3. Set baud to `115200`.
4. Set line ending to `Newline` (or `Both NL & CR`).
5. Paste one-line JSON and press Enter.
6. Press `BOOT` when prompted.

## JSON Rules (Important)

1. JSON must be on a single line.
2. Use standard double quotes `"`, never smart quotes.
3. Do not add stray characters outside JSON.
4. Keep required keys present.

## Minimal Wi-Fi JSON (chat_configurable)

```json
{"wifi_ssid":"YOUR_WIFI_SSID","wifi_password":"YOUR_WIFI_PASSWORD","subscription":"free","asr_api_key":"YOUR_ASR_KEY","asr_cluster":"volcengine_input_en","ai_provider":"gemini","gemini_apiKey":"YOUR_GEMINI_KEY","gemini_model":"gemini-2.0-flash","use_elevenlabs_tts":true,"elevenlabs_api_key":"YOUR_ELEVENLABS_KEY","elevenlabs_voice_id":"EST9Ui6982FZPSi7gCHi","elevenlabs_model_id":"eleven_flash_v2_5","system_prompt":"You are a helpful assistant.","vision_enabled":false,"memory_mode":"local"}
```

## Minimal Wi-Fi JSON (speaker_tts_test)

```json
{"wifi_ssid":"YOUR_WIFI_SSID","wifi_password":"YOUR_WIFI_PASSWORD","tts_api_key":"YOUR_OPENAI_API_KEY","tts_api_base_url":"https://api.openai.com","tts_model":"gpt-4o-mini-tts","tts_voice":"alloy","tts_speed":"1.0","speak_text":"Hello from speaker test","i2s_bclk":26,"i2s_lrc":25,"i2s_dout":27}
```

## Common Errors

- `Error: Missing required config`
  - One or more required keys are empty/missing.
  - JSON syntax invalid.

- Serial spam like `":22}`
  - Hardware/UART interference, often wrong wiring on UART/boot pins.
  - Re-check no connections on GPIO1/GPIO3/GPIO0/GPIO2/GPIO12/GPIO15/EN.

- Wi‑Fi never connects
  - Wrong SSID/password
  - captive enterprise network not supported by basic WPA flow
  - use phone hotspot for demos

## Team Standard

When sharing configs, use:
1. One-line JSON only
2. Redact API keys before posting in chat
3. Include board type and current serial port in message
