# Team Documentation Plan

Create these docs in this repo so firmware, hardware, and web teammates can work in parallel.

## Required Docs

1. `docs/SETUP_ESP32.md`
- Arduino CLI + Arduino IDE setup
- board profile and upload commands
- serial monitor commands

2. `docs/CONFIG_JSON_REFERENCE.md`
- valid JSON examples (speaker test, full runtime)
- required vs optional fields
- common formatting mistakes

3. `docs/WIRING_MAP.md`
- ESP32 classic pin map
- MAX98357 speaker amp wiring
- INMP441 mic wiring
- forbidden pins for boot/serial stability

4. `docs/RUNBOOK_DEBUGGING.md`
- upload failure troubleshooting
- serial spam troubleshooting (`":22}` symptom)
- Wi-Fi and API diagnostics

5. `docs/ARCHITECTURE_MEMORY_VISION.md`
- speech pipeline (ASR -> LLM -> TTS)
- visual context trigger policy (on turn + periodic)
- MongoDB Atlas storage/recall strategy via `backend/memory-api`

6. `docs/WEB_CONTROL_API.md`
- `/api/status`, `/api/config`, `/api/start`, `/api/stop`
- runtime command equivalents (`status`, `diag`, `testtts:`)

## Optional Docs

1. `docs/DEMO_CHECKLIST.md`
2. `docs/SECURITY_KEYS.md` (key rotation and handling)
3. `docs/TEAM_HANDOFF.md`

## Ownership Suggestion

- Firmware lead: `SETUP_ESP32`, `CONFIG_JSON_REFERENCE`, `RUNBOOK_DEBUGGING`
- Hardware lead: `WIRING_MAP`
- Backend/web lead: `WEB_CONTROL_API`, `ARCHITECTURE_MEMORY_VISION`
- PM/demo lead: `DEMO_CHECKLIST`, `TEAM_HANDOFF`
