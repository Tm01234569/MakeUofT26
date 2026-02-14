# ESP32 Setup (CLI + IDE)

This guide sets up and runs firmware in this repo on **ESP32 classic** (not S3).

## 1) Firmware Paths

- Main runtime: `firmware/examples/chat_configurable/chat_configurable.ino`
- Speaker test: `firmware/examples/speaker_tts_test/speaker_tts_test.ino`

## 2) Arduino CLI Compile Commands

### Main runtime

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --library /Users/shoadachi/Projects/MakeUofT/MakeUofT26/firmware \
  /Users/shoadachi/Projects/MakeUofT/MakeUofT26/firmware/examples/chat_configurable
```

### Speaker test

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml compile \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --library /Users/shoadachi/Projects/MakeUofT/MakeUofT26/firmware \
  /Users/shoadachi/Projects/MakeUofT/MakeUofT26/firmware/examples/speaker_tts_test
```

## 3) Upload Command

Pick active port from `board list` first.

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml board list
PORT=/dev/cu.usbserial-0001
```

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml upload \
  -p "$PORT" \
  --fqbn esp32:esp32:esp32 \
  --board-options PartitionScheme=huge_app \
  --upload-property upload.speed=115200 \
  /Users/shoadachi/Projects/MakeUofT/MakeUofT26/firmware/examples/chat_configurable
```

If upload fails, hold `BOOT`, start upload, tap `EN`, release `BOOT` after writing starts.

## 4) Serial Monitor

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml monitor \
  -p "$PORT" \
  -c baudrate=115200,dtr=off,rts=off
```

## 5) Arduino IDE Notes

- Board: `ESP32 Dev Module`
- Flash partition: large app (`huge_app` equivalent)
- Baud: `115200`
- Use Serial Monitor line ending: `Newline` or `Both NL & CR`

## 6) Runtime Serial Commands (chat_configurable)

- `status`
- `diag`
- `testtts:hello from robot`
- `provider:openai` / `provider:gemini`
- `model:<model-id>`
- `baseurl:<url>`
- `ttsbaseurl:<url>`
- `ttsmodel:<model-id>`
