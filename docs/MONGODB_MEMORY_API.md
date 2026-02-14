# MongoDB Memory API Contract

Firmware now uses a generic remote memory API client (`RemoteMemory`) so ESP32 does not talk to MongoDB directly.

## Architecture

1. ESP32 sends memory writes/recall requests to your API.
2. Backend API writes to MongoDB and runs vector search.
3. Backend returns compact memory text for prompt injection.

## Why this design

- Keeps ESP32 code simple and stable.
- Lets backend handle embeddings, chunking, and ranking.
- Easy to swap model/provider without reflashing firmware.

## Required Firmware JSON Fields

```json
{"memory_api_url":"https://memory-api.example.com","memory_api_key":"YOUR_MEMORY_API_KEY","memory_mode":"both"}
```

`memory_mode` options:
- `local`: no remote memory
- `remote`: remote memory only
- `both`: local + remote

## Endpoints Expected By Firmware

Base URL: `memory_api_url`

1. `POST /v1/memory/conversations`
- body:
```json
{"device_id":"...","user_message":"...","assistant_message":"...","ai_provider":"openai","visual_context":"optional"}
```

2. `POST /v1/memory/visual-events`
- body:
```json
{"device_id":"...","description":"...","event_type":"user_turn"}
```

3. `POST /v1/memory/recall`
- body:
```json
{"query":"...","device_id":"...","top_conversations":5,"top_visual_events":3}
```
- response: plain text snippet (or JSON string field your firmware expects to append to prompt)

## Headers Sent By Firmware

- `Content-Type: application/json`
- `x-api-key: <memory_api_key>`
- `Authorization: Bearer <memory_api_key>`
- `x-device-id: <device_id>`

## Vectorization Recommendation

Vectorize these on backend:
1. user message
2. assistant message
3. visual description text (from Gemini vision summary)

Then merge top-k recall results into one concise text block returned to ESP32.
