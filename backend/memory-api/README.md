# Memory API (MongoDB Atlas)

Backend for ESP32 memory features.

## Endpoints

- `POST /v1/memory/conversations`
- `POST /v1/memory/visual-events`
- `POST /v1/memory/recall`
- `POST /v1/asr/transcribe` (raw PCM16 -> Gemini transcription)
- `POST /v1/asr/stream/start` (open ASR stream session)
- `POST /v1/asr/stream/chunk` (append PCM16 chunk)
- `POST /v1/asr/stream/stop` (close stream + transcribe)
- `POST /v1/asr/stream/abort` (discard stream)
- `POST /v1/llm/chat` (Gemini text/vision)
- `POST /v1/tts/synthesize` (ElevenLabs audio)
- `GET /healthz`

## 1) Install

```bash
cd backend/memory-api
npm install
cp .env.example .env
```

## 2) Configure `.env`

Required:
- `MEMORY_API_KEY`
- `MONGODB_URI`
- `MONGODB_DB`
- `MONGODB_COLLECTION`

Recommended for vector retrieval:
- `GEMINI_API_KEY`
- `GEMINI_ASR_MODEL=gemini-2.0-flash`
- `GEMINI_LLM_MODEL=gemini-2.0-flash`
- `GEMINI_EMBEDDING_MODEL=models/gemini-embedding-001`
- `GEMINI_EMBEDDING_DIM=768`

Required for backend TTS proxy:
- `ELEVENLABS_API_KEY`
- `ELEVENLABS_TTS_MODEL=eleven_flash_v2_5`

## 3) Run

```bash
npm run dev
```

## 4) Atlas Vector Index

Create Atlas Vector Search index on collection `${MONGODB_COLLECTION}` with name `memory_vector_index` and definition:

```json
{
  "fields": [
    {
      "type": "vector",
      "path": "embedding",
      "numDimensions": 768,
      "similarity": "cosine"
    },
    {
      "type": "filter",
      "path": "device_id"
    },
    {
      "type": "filter",
      "path": "kind"
    }
  ]
}
```

If vector index is not ready, API falls back to lexical recall.

## 5) ESP32 JSON Fields

Use these in firmware config:

```json
{
  "memory_api_url": "https://your-api.example.com",
  "memory_api_key": "same-as-MEMORY_API_KEY",
  "memory_mode": "both"
}
```

For backend ASR proxy mode in firmware:

```json
{
  "asr_provider": "backend",
  "asr_api_url": "http://<your-laptop-ip>:8787",
  "asr_api_key": "same-as-MEMORY_API_KEY"
}
```

For full backend proxy mode (ASR + LLM + TTS) in firmware:

```json
{
  "asr_provider": "backend",
  "asr_api_url": "http://<your-laptop-ip>:8787",
  "asr_api_key": "same-as-MEMORY_API_KEY",
  "ai_provider": "backend",
  "backend_api_url": "http://<your-laptop-ip>:8787",
  "backend_api_key": "same-as-MEMORY_API_KEY",
  "use_backend_tts": true
}
```

## Notes

- This service is Atlas-ready but can run even if Mongo is temporarily unavailable.
- Firmware expects `recall` response as plain text.
