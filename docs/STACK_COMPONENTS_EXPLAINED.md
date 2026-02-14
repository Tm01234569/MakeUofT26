# Stack Components Explained

This explains each moving part in your current architecture.

## Hardware + Firmware

- `ESP32`
Purpose: Runs on-device control loop (button, mic, speaker, networking).

- `INMP441 mic`
Purpose: Captures user voice input.

- `MAX98357 + speaker`
Purpose: Plays synthesized voice audio output.

## AI Pipeline

- `ASR (ByteDance/Volcengine)`
Purpose: Converts mic audio to text.
Why: Current code already supports stable streaming ASR on ESP32.

- `Gemini (LLM + vision)`
Purpose: Generates text replies and vision summaries from camera input.

- `ElevenLabs (TTS)`
Purpose: Converts final reply text to speech audio.

## Memory + Context

- `MongoDB Atlas`
Purpose: Stores conversation events + visual events.

- `Vector Search`
Purpose: Retrieves semantically related memories for new user queries.
Data vectorized: user text, assistant text, visual description text.

- `Remote Memory API` (`backend/memory-api`)
Purpose: Secure middle layer between ESP32 and Atlas.
Why: ESP32 should call simple HTTPS endpoints, not raw DB.

## Endpoints (Remote Memory API)

- `POST /v1/memory/conversations`
Stores user+assistant turn.

- `POST /v1/memory/visual-events`
Stores vision summary events.

- `POST /v1/memory/recall`
Returns relevant memory text for prompt injection.

## Runtime Modes

- `memory_mode=local`
No remote DB recall/store.

- `memory_mode=remote`
Use remote memory only.

- `memory_mode=both`
Use local + remote memory flow.

## Why this architecture fits MLH/demo

- Clear multimodal story: vision + speech + memory.
- Atlas integration is explicit and measurable.
- Components are swappable without rewriting whole firmware.
- Debuggable with serial commands and web status API.
