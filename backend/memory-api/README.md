# Memory API (MongoDB Atlas)

Backend for ESP32 memory features.

## Endpoints

- `POST /v1/memory/conversations`
- `POST /v1/memory/visual-events`
- `POST /v1/memory/recall`
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
- `GEMINI_EMBEDDING_MODEL=models/gemini-embedding-001`
- `GEMINI_EMBEDDING_DIM=768`

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

## Notes

- This service is Atlas-ready but can run against any MongoDB connection string.
- Firmware expects `recall` response as plain text.
