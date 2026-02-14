# MongoDB Atlas Setup Runbook

This sets up Atlas for the memory API used by firmware.

## 1) Create Atlas Project + Cluster

1. Create project (e.g., `MakeUofT26`).
2. Create M0/M10 cluster.
3. Region close to your demo location.

## 2) Create Database User

1. Atlas -> Database Access -> Add New Database User.
2. Give read/write on your app DB (e.g., `makeuoft26`).
3. Save username/password.

## 3) Network Access

1. Atlas -> Network Access.
2. Add API host IP(s).
3. For hackathon speed: temporary `0.0.0.0/0` (tighten later).

## 4) Connection String

Get SRV URI from Atlas Connect panel:

```text
mongodb+srv://<user>:<pass>@<cluster>/<db>?retryWrites=true&w=majority
```

Use this as `MONGODB_URI` in backend `.env`.

## 5) Create Atlas Vector Search Index

Collection: `memory_events` (or your chosen `MONGODB_COLLECTION`)
Index name: `memory_vector_index`

Index JSON:

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

## 6) Backend Env Example

In `backend/memory-api/.env`:

```env
PORT=8787
MEMORY_API_KEY=replace-me
MONGODB_URI=mongodb+srv://<user>:<pass>@<cluster>/<db>?retryWrites=true&w=majority
MONGODB_DB=makeuoft26
MONGODB_COLLECTION=memory_events
ATLAS_VECTOR_INDEX=memory_vector_index
GEMINI_API_KEY=your-gemini-key
GEMINI_EMBEDDING_MODEL=models/gemini-embedding-001
GEMINI_EMBEDDING_DIM=768
```

## 7) Run Backend

```bash
cd backend/memory-api
npm install
npm run dev
```

Health check:

```bash
curl -s http://localhost:8787/healthz
```

## 8) Firmware Config

Use API URL and key:

```json
{"memory_api_url":"https://your-api-host","memory_api_key":"replace-me","memory_mode":"both"}
```

## 9) Smoke Test (API)

```bash
API=http://localhost:8787
KEY=replace-me

curl -s -X POST "$API/v1/memory/conversations" \
  -H "x-api-key: $KEY" -H "Content-Type: application/json" \
  -d '{"device_id":"demo-esp32","user_message":"my name is sho","assistant_message":"nice to meet you","ai_provider":"gemini"}'

curl -s -X POST "$API/v1/memory/visual-events" \
  -H "x-api-key: $KEY" -H "Content-Type: application/json" \
  -d '{"device_id":"demo-esp32","description":"user is smiling near desk lamp","event_type":"user_turn"}'

curl -s -X POST "$API/v1/memory/recall" \
  -H "x-api-key: $KEY" -H "Content-Type: application/json" \
  -d '{"device_id":"demo-esp32","query":"what do you remember about me?","top_conversations":5,"top_visual_events":3}'
```
