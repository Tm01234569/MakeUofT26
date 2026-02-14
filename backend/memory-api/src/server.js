import 'dotenv/config';
import express from 'express';
import { MongoClient } from 'mongodb';

const {
  PORT = '8787',
  MEMORY_API_KEY = '',
  MONGODB_URI = '',
  MONGODB_DB = 'makeuoft26',
  MONGODB_COLLECTION = 'memory_events',
  ATLAS_VECTOR_INDEX = 'memory_vector_index',
  GEMINI_API_KEY = '',
  GEMINI_EMBEDDING_MODEL = 'models/gemini-embedding-001',
  GEMINI_EMBEDDING_DIM = '768'
} = process.env;

if (!MONGODB_URI) {
  console.error('[memory-api] Missing MONGODB_URI');
  process.exit(1);
}
if (!MEMORY_API_KEY) {
  console.error('[memory-api] Missing MEMORY_API_KEY');
  process.exit(1);
}

const app = express();
app.use(express.json({ limit: '1mb' }));

const mongo = new MongoClient(MONGODB_URI);
await mongo.connect();
const db = mongo.db(MONGODB_DB);
const collection = db.collection(MONGODB_COLLECTION);

await collection.createIndex({ device_id: 1, kind: 1, created_at: -1 });

function readApiKey(req) {
  const header = req.header('x-api-key') || '';
  if (header) return header.trim();
  const auth = req.header('authorization') || '';
  if (auth.toLowerCase().startsWith('bearer ')) {
    return auth.slice(7).trim();
  }
  return '';
}

function auth(req, res, next) {
  const key = readApiKey(req);
  if (!key || key !== MEMORY_API_KEY) {
    res.status(401).json({ ok: false, error: 'unauthorized' });
    return;
  }
  next();
}

function safeString(value, fallback = '') {
  if (typeof value !== 'string') return fallback;
  return value.trim();
}

function nowIso() {
  return new Date().toISOString();
}

async function embedText(text, taskType = 'RETRIEVAL_DOCUMENT') {
  if (!GEMINI_API_KEY) return null;

  const body = {
    model: GEMINI_EMBEDDING_MODEL,
    content: { parts: [{ text }] },
    taskType,
    outputDimensionality: Number(GEMINI_EMBEDDING_DIM)
  };

  const r = await fetch(`https://generativelanguage.googleapis.com/v1beta/${GEMINI_EMBEDDING_MODEL}:embedContent`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'x-goog-api-key': GEMINI_API_KEY
    },
    body: JSON.stringify(body)
  });

  if (!r.ok) {
    const err = await r.text();
    console.warn('[memory-api] embed failed', r.status, err.slice(0, 300));
    return null;
  }

  const data = await r.json();
  const values = data?.embedding?.values;
  if (!Array.isArray(values) || values.length === 0) return null;
  return values;
}

async function vectorRecall({ deviceId, query, limit, kind }) {
  const qEmb = await embedText(query, 'RETRIEVAL_QUERY');
  if (!qEmb) return [];

  const pipeline = [
    {
      $vectorSearch: {
        index: ATLAS_VECTOR_INDEX,
        path: 'embedding',
        queryVector: qEmb,
        numCandidates: Math.max(50, limit * 15),
        limit,
        filter: { device_id: deviceId, kind }
      }
    },
    {
      $project: {
        _id: 0,
        kind: 1,
        text: 1,
        created_at: 1,
        score: { $meta: 'vectorSearchScore' }
      }
    }
  ];

  try {
    return await collection.aggregate(pipeline).toArray();
  } catch (err) {
    console.warn('[memory-api] vector search fallback', String(err).slice(0, 400));
    return [];
  }
}

async function lexicalRecall({ deviceId, query, limit, kind }) {
  const docs = await collection
    .find({ device_id: deviceId, kind, text: { $regex: query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), $options: 'i' } })
    .project({ _id: 0, kind: 1, text: 1, created_at: 1 })
    .sort({ created_at: -1 })
    .limit(limit)
    .toArray();
  return docs;
}

app.get('/healthz', (_req, res) => {
  res.json({ ok: true, ts: nowIso() });
});

app.post('/v1/memory/conversations', auth, async (req, res) => {
  try {
    const deviceId = safeString(req.body?.device_id);
    const userMessage = safeString(req.body?.user_message);
    const assistantMessage = safeString(req.body?.assistant_message);
    const aiProvider = safeString(req.body?.ai_provider, 'unknown');
    const visualContext = safeString(req.body?.visual_context);

    if (!deviceId || !userMessage || !assistantMessage) {
      res.status(400).json({ ok: false, error: 'missing_required_fields' });
      return;
    }

    const text = `User: ${userMessage}\nAssistant: ${assistantMessage}${visualContext ? `\nVisual: ${visualContext}` : ''}`;
    const embedding = await embedText(text, 'RETRIEVAL_DOCUMENT');

    await collection.insertOne({
      kind: 'conversation',
      device_id: deviceId,
      user_message: userMessage,
      assistant_message: assistantMessage,
      ai_provider: aiProvider,
      visual_context: visualContext || null,
      text,
      embedding,
      created_at: new Date()
    });

    res.status(200).json({ ok: true });
  } catch (err) {
    console.error('[memory-api] conversations error', err);
    res.status(500).json({ ok: false, error: 'internal_error' });
  }
});

app.post('/v1/memory/visual-events', auth, async (req, res) => {
  try {
    const deviceId = safeString(req.body?.device_id);
    const description = safeString(req.body?.description);
    const eventType = safeString(req.body?.event_type, 'observation');

    if (!deviceId || !description) {
      res.status(400).json({ ok: false, error: 'missing_required_fields' });
      return;
    }

    const embedding = await embedText(description, 'RETRIEVAL_DOCUMENT');

    await collection.insertOne({
      kind: 'visual_event',
      device_id: deviceId,
      event_type: eventType,
      description,
      text: description,
      embedding,
      created_at: new Date()
    });

    res.status(200).json({ ok: true });
  } catch (err) {
    console.error('[memory-api] visual-events error', err);
    res.status(500).json({ ok: false, error: 'internal_error' });
  }
});

app.post('/v1/memory/recall', auth, async (req, res) => {
  try {
    const query = safeString(req.body?.query);
    const deviceId = safeString(req.body?.device_id);
    const topConversations = Number(req.body?.top_conversations ?? 5);
    const topVisualEvents = Number(req.body?.top_visual_events ?? 3);

    if (!query || !deviceId) {
      res.status(400).type('text/plain').send('');
      return;
    }

    let conversations = await vectorRecall({
      deviceId,
      query,
      limit: Math.max(1, Math.min(10, topConversations)),
      kind: 'conversation'
    });

    let visualEvents = await vectorRecall({
      deviceId,
      query,
      limit: Math.max(1, Math.min(10, topVisualEvents)),
      kind: 'visual_event'
    });

    if (conversations.length === 0) {
      conversations = await lexicalRecall({
        deviceId,
        query,
        limit: Math.max(1, Math.min(10, topConversations)),
        kind: 'conversation'
      });
    }
    if (visualEvents.length === 0) {
      visualEvents = await lexicalRecall({
        deviceId,
        query,
        limit: Math.max(1, Math.min(10, topVisualEvents)),
        kind: 'visual_event'
      });
    }

    const convoLines = conversations.map((d, i) => `[C${i + 1}] ${d.text}`);
    const visualLines = visualEvents.map((d, i) => `[V${i + 1}] ${d.text}`);
    const out = [...convoLines, ...visualLines].join('\n');

    res.status(200).type('text/plain').send(out);
  } catch (err) {
    console.error('[memory-api] recall error', err);
    res.status(500).type('text/plain').send('');
  }
});

app.listen(Number(PORT), () => {
  console.log(`[memory-api] listening on :${PORT}`);
});
