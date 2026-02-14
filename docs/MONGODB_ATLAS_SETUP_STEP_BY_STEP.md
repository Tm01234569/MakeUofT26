# MongoDB Atlas Setup (Step-by-Step)

This guide is the exact setup flow for your project backend (`backend/memory-api`).

## 1) Create Atlas Project + Cluster

1. Log in to MongoDB Atlas.
2. Create a new project (example: `MakeUofT26`).
3. Create a cluster (M0 is fine for hackathon testing).
4. Pick a region close to your demo location.

## 2) Create Database User

1. Go to `Database Access`.
2. Click `Add New Database User`.
3. Set username/password.
4. Grant readWrite permission on your app database (example: `makeuoft26`).
5. Save credentials.

## 3) Allow Network Access

1. Go to `Network Access`.
2. Add your backend server IP.
3. For rapid testing, temporarily allow `0.0.0.0/0`.
4. Tighten this after demo.

## 4) Get Connection String

1. Click `Connect` on cluster.
2. Choose drivers / application connection string.
3. Copy SRV URI:

```text
mongodb+srv://<user>:<pass>@<cluster>/<db>?retryWrites=true&w=majority
```

## 5) Configure Backend Env

Open `backend/memory-api/.env` and set:

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

## 6) Create Atlas Vector Search Index

Collection: `memory_events`
Index name: `memory_vector_index`

Definition:

```json
{
  "fields": [
    { "type": "vector", "path": "embedding", "numDimensions": 768, "similarity": "cosine" },
    { "type": "filter", "path": "device_id" },
    { "type": "filter", "path": "kind" }
  ]
}
```

## 7) Run Backend

```bash
cd /Users/shoadachi/Projects/MakeUofT/MakeUofT26/backend/memory-api
npm install
npm run dev
```

Health check:

```bash
curl -s http://localhost:8787/healthz
```

## 8) Connect Firmware

In ESP32 JSON config include:

```json
{"memory_api_url":"https://your-api-host","memory_api_key":"replace-me","memory_mode":"both"}
```

## 9) Quick API Smoke Test

```bash
API=http://localhost:8787
KEY=replace-me

curl -s -X POST "$API/v1/memory/conversations" -H "x-api-key: $KEY" -H "Content-Type: application/json" -d '{"device_id":"demo-esp32","user_message":"my name is sho","assistant_message":"nice to meet you","ai_provider":"gemini"}'

curl -s -X POST "$API/v1/memory/visual-events" -H "x-api-key: $KEY" -H "Content-Type: application/json" -d '{"device_id":"demo-esp32","description":"user is smiling near desk lamp","event_type":"user_turn"}'

curl -s -X POST "$API/v1/memory/recall" -H "x-api-key: $KEY" -H "Content-Type: application/json" -d '{"device_id":"demo-esp32","query":"what do you remember about me?","top_conversations":5,"top_visual_events":3}'
```
