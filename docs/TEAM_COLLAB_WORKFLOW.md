# Team Collaboration Workflow (Hackathon-safe)

## Branch Strategy

1. `main` is always demoable.
2. Use short-lived branches:
- `firmware/<task>`
- `hardware/<task>`
- `web/<task>`
- `docs/<task>`

## Daily Merge Rhythm

1. Morning: sync from `main`
2. Midday: one integration merge
3. Pre-demo: freeze and only bugfix merges

## Definition Of Done (Firmware)

1. Compiles with exact command in docs
2. Uploads on ESP32 classic
3. `diag` command returns sane data
4. Speaker test passes
5. One full ASR->LLM->TTS roundtrip passes

## Shared Interface Contract

- Config input: serial JSON
- Runtime control: serial commands + web control API
- Memory backend: MongoDB Atlas via `backend/memory-api` (optional mode)

## Immediate Next Actions

1. Copy files listed in `docs/TRANSFER_PLAN.md`.
2. Commit as `chore: import dazi firmware baseline`.
3. Add wiring and config docs before more feature work.
4. Run one end-to-end smoke test and log results.
