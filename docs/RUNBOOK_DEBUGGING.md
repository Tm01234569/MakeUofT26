# Debugging Runbook

## 1) Upload Fails (`Failed to connect` / `Invalid head of packet`)

1. Disconnect breadboard/peripherals.
2. Upload with only USB connected.
3. Reconnect peripherals after flash.
4. Avoid boot/uart pins for external wiring:
- `GPIO0`, `GPIO2`, `GPIO12`, `GPIO15`, `EN`
- `GPIO1(TX0)`, `GPIO3(RX0)`

## 2) Serial Port Missing

```bash
arduino-cli --config-file /Users/shoadachi/Projects/MakeUofT/.arduino-cli.yaml board list
ls /dev/cu.usb* /dev/tty.usb* 2>/dev/null
```

- Replug USB cable
- Press `EN`
- Ensure data-capable cable

## 3) Monitor Floods with `":22}`

This indicates serial corruption/echo from wiring interference.

Actions:
1. Remove all wires, verify clean boot log.
2. Re-add wiring step-by-step.
3. Ensure nothing on TX/RX and boot strap pins.
4. Move I2S DOUT to safer pin (e.g. GPIO27) if needed.

## 4) No Speaker Audio but TTS says playing

1. Verify amp wiring and shared ground.
2. Verify speaker is connected to amp `SPK+/-` (not ESP32 directly).
3. Use serial command: `testtts:hello`
4. Use serial command: `diag`

## 5) Wi-Fi Config Errors

- If `Error: Missing required config`, check required keys.
- Send one-line JSON only.
- Use ASCII double quotes.

## 6) Web Control Verification

If `web_control_enabled:true` and Wi-Fi connected:

- `http://<ESP32_IP>/api/status`
- `http://<ESP32_IP>/api/config`

`/api/status` includes vision debug fields:
- last reason
- last store decision
- last context preview
- store count in last hour

## 7) Fast Triage Commands (serial)

- `status`
- `diag`
- `testtts:system check`

## 8) Remote Memory (Mongo API) Checks

1. Set `memory_mode` to `remote` or `both`.
2. Confirm `memory_api_url` and `memory_api_key` are present in JSON config.
3. Run `diag` and verify runtime is stable.
4. During conversation, check API logs for:
- `POST /v1/memory/conversations`
- `POST /v1/memory/visual-events`
- `POST /v1/memory/recall`
