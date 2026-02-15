import 'dotenv/config';
import express from 'express';
import { MongoClient } from 'mongodb';
import { randomUUID } from 'crypto';

const {
  PORT = '8787',
  MEMORY_API_KEY = '',
  MONGODB_URI = '',
  MONGODB_DB = 'makeuoft26',
  MONGODB_COLLECTION = 'memory_events',
  ATLAS_VECTOR_INDEX = 'memory_vector_index',
  GEMINI_API_KEY = '',
  GEMINI_ASR_MODEL = 'gemini-2.0-flash',
  GEMINI_LLM_MODEL = 'gemini-2.0-flash',
  GEMINI_EMBEDDING_MODEL = 'models/gemini-embedding-001',
  GEMINI_EMBEDDING_DIM = '768',
  ELEVENLABS_API_KEY = '',
  ELEVENLABS_TTS_MODEL = 'eleven_flash_v2_5',
  FETCH_TIMEOUT_MS = '30000'
} = process.env;

if (!MEMORY_API_KEY) {
  console.error('[memory-api] Missing MEMORY_API_KEY');
  process.exit(1);
}

const app = express();
app.use(express.json({ limit: '8mb' }));

let mongo = null;
let collection = null;
let mongoConnected = false;
const asrSessions = new Map();

async function initMongo() {
  if (!MONGODB_URI) {
    console.warn('[memory-api] MONGODB_URI missing: memory endpoints disabled');
    return;
  }

  try {
    mongo = new MongoClient(MONGODB_URI);
    await mongo.connect();
    const db = mongo.db(MONGODB_DB);
    collection = db.collection(MONGODB_COLLECTION);
    await collection.createIndex({ device_id: 1, kind: 1, created_at: -1 });
    mongoConnected = true;
    console.log(`[memory-api] mongo connected db=${MONGODB_DB} collection=${MONGODB_COLLECTION}`);
  } catch (err) {
    mongoConnected = false;
    collection = null;
    console.warn('[memory-api] mongo connect failed; memory endpoints disabled:', String(err).slice(0, 400));
  }
}

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

function pruneAsrSessions(maxAgeMs = 10 * 60 * 1000) {
  const now = Date.now();
  for (const [sid, session] of asrSessions.entries()) {
    const age = now - (session?.updatedAt ?? now);
    if (age > maxAgeMs) {
      asrSessions.delete(sid);
    }
  }
}

function ensureMemoryStore(res) {
  if (!collection) {
    res.status(503).json({ ok: false, error: 'memory_store_unavailable' });
    return false;
  }
  return true;
}

async function fetchWithTimeout(url, options = {}, timeoutMs = Number(FETCH_TIMEOUT_MS)) {
  const timeout = Number.isFinite(timeoutMs) && timeoutMs > 0 ? timeoutMs : 30000;
  return fetch(url, {
    ...options,
    signal: AbortSignal.timeout(timeout)
  });
}

function pcm16ToWavBuffer(pcmBuffer, sampleRate = 16000, channels = 1, bitsPerSample = 16) {
  const dataSize = pcmBuffer.length;
  const header = Buffer.alloc(44);
  const byteRate = sampleRate * channels * (bitsPerSample / 8);
  const blockAlign = channels * (bitsPerSample / 8);

  header.write('RIFF', 0);
  header.writeUInt32LE(36 + dataSize, 4);
  header.write('WAVE', 8);
  header.write('fmt ', 12);
  header.writeUInt32LE(16, 16);
  header.writeUInt16LE(1, 20);
  header.writeUInt16LE(channels, 22);
  header.writeUInt32LE(sampleRate, 24);
  header.writeUInt32LE(byteRate, 28);
  header.writeUInt16LE(blockAlign, 32);
  header.writeUInt16LE(bitsPerSample, 34);
  header.write('data', 36);
  header.writeUInt32LE(dataSize, 40);

  return Buffer.concat([header, pcmBuffer]);
}

function geminiTextFromResponse(data) {
  const parts = data?.candidates?.[0]?.content?.parts;
  if (!Array.isArray(parts)) return '';
  const text = parts
    .map((p) => (typeof p?.text === 'string' ? p.text : ''))
    .join('')
    .trim();
  return text;
}

async function embedText(text, taskType = 'RETRIEVAL_DOCUMENT') {
  if (!GEMINI_API_KEY || !text) return null;

  const body = {
    model: GEMINI_EMBEDDING_MODEL,
    content: { parts: [{ text }] },
    taskType,
    outputDimensionality: Number(GEMINI_EMBEDDING_DIM)
  };

  const r = await fetchWithTimeout(
    `https://generativelanguage.googleapis.com/v1beta/${GEMINI_EMBEDDING_MODEL}:embedContent`,
    {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'x-goog-api-key': GEMINI_API_KEY
      },
      body: JSON.stringify(body)
    }
  );

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

async function transcribePcmWithGemini(pcmBuffer, { sampleRate = 16000, channels = 1, bitsPerSample = 16 } = {}) {
  if (!GEMINI_API_KEY) return '';

  const wav = pcm16ToWavBuffer(pcmBuffer, sampleRate, channels, bitsPerSample);
  const b64 = wav.toString('base64');

  const body = {
    contents: [
      {
        parts: [
          { text: 'Transcribe this spoken audio. Return plain text only.' },
          {
            inline_data: {
              mime_type: 'audio/wav',
              data: b64
            }
          }
        ]
      }
    ],
    generationConfig: {
      temperature: 0
    }
  };

  const r = await fetchWithTimeout(
    `https://generativelanguage.googleapis.com/v1beta/models/${GEMINI_ASR_MODEL}:generateContent?key=${encodeURIComponent(GEMINI_API_KEY)}`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    }
  );

  if (!r.ok) {
    const err = await r.text();
    console.warn('[memory-api] asr failed', r.status, err.slice(0, 300));
    return '';
  }

  const data = await r.json();
  return geminiTextFromResponse(data);
}

async function generateWithGemini({ model, systemPrompt, userMessage, history, imageBase64, imageMimeType }) {
  if (!GEMINI_API_KEY) {
    throw new Error('missing_gemini_api_key');
  }

  const m = safeString(model, GEMINI_LLM_MODEL) || GEMINI_LLM_MODEL;
  const parts = [];

  if (systemPrompt) {
    parts.push({ text: `System instruction:\n${systemPrompt}` });
  }

  if (Array.isArray(history) && history.length > 0) {
    const compact = history
      .slice(-6)
      .map((h) => {
        const u = safeString(h?.user);
        const a = safeString(h?.assistant);
        if (!u && !a) return '';
        return `User: ${u}\nAssistant: ${a}`;
      })
      .filter(Boolean)
      .join('\n\n');
    if (compact) {
      parts.push({ text: `Conversation history:\n${compact}` });
    }
  }

  const promptText = safeString(userMessage);
  if (promptText) {
    parts.push({ text: promptText });
  }

  if (imageBase64) {
    parts.push({
      inline_data: {
        mime_type: safeString(imageMimeType, 'image/jpeg') || 'image/jpeg',
        data: imageBase64
      }
    });
  }

  const body = {
    contents: [{ parts }],
    generationConfig: {
      temperature: 0.3
    }
  };

  const r = await fetchWithTimeout(
    `https://generativelanguage.googleapis.com/v1beta/models/${encodeURIComponent(m)}:generateContent?key=${encodeURIComponent(GEMINI_API_KEY)}`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    }
  );

  if (!r.ok) {
    const err = await r.text();
    throw new Error(`gemini_http_${r.status}:${err.slice(0, 300)}`);
  }

  const data = await r.json();
  return {
    text: geminiTextFromResponse(data),
    model: m
  };
}

async function vectorRecall({ deviceId, query, limit, kind }) {
  const qEmb = await embedText(query, 'RETRIEVAL_QUERY');
  if (!qEmb || !collection) return [];

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
  if (!collection) return [];
  const docs = await collection
    .find({ device_id: deviceId, kind, text: { $regex: query.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), $options: 'i' } })
    .project({ _id: 0, kind: 1, text: 1, created_at: 1 })
    .sort({ created_at: -1 })
    .limit(limit)
    .toArray();
  return docs;
}

app.get('/healthz', (_req, res) => {
  res.json({ ok: true, ts: nowIso(), mongo_connected: mongoConnected });
});

app.post('/v1/asr/transcribe', auth, express.raw({ type: 'application/octet-stream', limit: '1024kb' }), async (req, res) => {
  try {
    const sampleRate = Math.max(8000, Math.min(24000, Number(req.query.sample_rate ?? 16000)));
    const channels = Math.max(1, Math.min(2, Number(req.query.channels ?? 1)));
    const bitsPerSample = Number(req.query.bits ?? 16);

    if (!req.body || !Buffer.isBuffer(req.body) || req.body.length === 0) {
      res.status(400).json({ ok: false, error: 'empty_audio' });
      return;
    }

    if (bitsPerSample !== 16) {
      res.status(400).json({ ok: false, error: 'only_pcm16_supported' });
      return;
    }

    const text = await transcribePcmWithGemini(req.body, { sampleRate, channels, bitsPerSample });
    res.status(200).json({ ok: true, text });
  } catch (err) {
    console.error('[memory-api] asr error', err);
    res.status(500).json({ ok: false, error: 'internal_error' });
  }
});

app.post('/v1/asr/stream/start', auth, async (req, res) => {
  try {
    pruneAsrSessions();
    const sampleRate = Math.max(8000, Math.min(24000, Number(req.body?.sample_rate ?? 16000)));
    const channels = Math.max(1, Math.min(2, Number(req.body?.channels ?? 1)));
    const bitsPerSample = Number(req.body?.bits ?? 16);
    if (bitsPerSample !== 16) {
      res.status(400).json({ ok: false, error: 'only_pcm16_supported' });
      return;
    }

    const sessionId = randomUUID();
    asrSessions.set(sessionId, {
      createdAt: Date.now(),
      updatedAt: Date.now(),
      sampleRate,
      channels,
      bitsPerSample,
      chunks: [],
      bytes: 0
    });
    res.status(200).json({ ok: true, session_id: sessionId });
  } catch (err) {
    console.error('[memory-api] asr stream start error', err);
    res.status(500).json({ ok: false, error: 'internal_error' });
  }
});

app.post('/v1/asr/stream/chunk', auth, express.raw({ type: 'application/octet-stream', limit: '256kb' }), async (req, res) => {
  try {
    const sessionId = safeString(req.query?.session_id);
    const session = asrSessions.get(sessionId);
    if (!session) {
      res.status(404).json({ ok: false, error: 'session_not_found' });
      return;
    }
    if (!req.body || !Buffer.isBuffer(req.body) || req.body.length === 0) {
      res.status(400).json({ ok: false, error: 'empty_chunk' });
      return;
    }
    session.chunks.push(Buffer.from(req.body));
    session.bytes += req.body.length;
    session.updatedAt = Date.now();
    res.status(200).json({ ok: true, bytes: session.bytes });
  } catch (err) {
    console.error('[memory-api] asr stream chunk error', err);
    res.status(500).json({ ok: false, error: 'internal_error' });
  }
});

app.post('/v1/asr/stream/stop', auth, async (req, res) => {
  try {
    const sessionId = safeString(req.query?.session_id);
    const session = asrSessions.get(sessionId);
    if (!session) {
      res.status(404).json({ ok: false, error: 'session_not_found' });
      return;
    }

    asrSessions.delete(sessionId);
    if (session.bytes === 0) {
      res.status(200).json({ ok: true, text: '' });
      return;
    }

    const pcm = Buffer.concat(session.chunks, session.bytes);
    const text = await transcribePcmWithGemini(pcm, {
      sampleRate: session.sampleRate,
      channels: session.channels,
      bitsPerSample: session.bitsPerSample
    });
    res.status(200).json({ ok: true, text });
  } catch (err) {
    console.error('[memory-api] asr stream stop error', err);
    res.status(500).json({ ok: false, error: 'internal_error' });
  }
});

app.post('/v1/asr/stream/abort', auth, async (req, res) => {
  try {
    const sessionId = safeString(req.query?.session_id);
    if (sessionId.length > 0) {
      asrSessions.delete(sessionId);
    }
    res.status(200).json({ ok: true });
  } catch (err) {
    console.error('[memory-api] asr stream abort error', err);
    res.status(500).json({ ok: false, error: 'internal_error' });
  }
});

app.post('/v1/llm/chat', auth, async (req, res) => {
  try {
    const model = safeString(req.body?.model, GEMINI_LLM_MODEL);
    const systemPrompt = safeString(req.body?.system_prompt);
    const userMessage = safeString(req.body?.user_message);
    const history = Array.isArray(req.body?.history) ? req.body.history : [];
    const imageBase64 = safeString(req.body?.image_base64);
    const imageMimeType = safeString(req.body?.image_mime_type, 'image/jpeg') || 'image/jpeg';

    if (!userMessage && !imageBase64) {
      res.status(400).json({ ok: false, error: 'missing_user_message' });
      return;
    }

    const out = await generateWithGemini({
      model,
      systemPrompt,
      userMessage,
      history,
      imageBase64,
      imageMimeType
    });

    res.status(200).json({ ok: true, text: out.text, model: out.model });
  } catch (err) {
    console.error('[memory-api] llm error', err);
    res.status(500).json({ ok: false, error: 'llm_failed', detail: String(err).slice(0, 400) });
  }
});

app.post('/v1/tts/synthesize', auth, async (req, res) => {
  try {
    if (!ELEVENLABS_API_KEY) {
      res.status(500).json({ ok: false, error: 'missing_elevenlabs_api_key' });
      return;
    }

    const text = safeString(req.body?.text);
    const voiceId = safeString(req.body?.voice_id, 'EST9Ui6982FZPSi7gCHi') || 'EST9Ui6982FZPSi7gCHi';
    const modelId = safeString(req.body?.model_id, ELEVENLABS_TTS_MODEL) || ELEVENLABS_TTS_MODEL;
    const outputFormat = safeString(req.body?.output_format, 'mp3_22050_32') || 'mp3_22050_32';

    if (!text) {
      res.status(400).json({ ok: false, error: 'missing_text' });
      return;
    }

    const url = `https://api.elevenlabs.io/v1/text-to-speech/${encodeURIComponent(voiceId)}?output_format=${encodeURIComponent(outputFormat)}`;
    const r = await fetchWithTimeout(url, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        Accept: 'audio/mpeg',
        'xi-api-key': ELEVENLABS_API_KEY
      },
      body: JSON.stringify({
        text,
        model_id: modelId
      })
    });

    if (!r.ok) {
      const err = await r.text();
      res.status(r.status).json({ ok: false, error: 'elevenlabs_failed', detail: err.slice(0, 500) });
      return;
    }

    const ab = await r.arrayBuffer();
    const buf = Buffer.from(ab);
    res.setHeader('Content-Type', 'audio/mpeg');
    res.setHeader('Content-Length', String(buf.length));
    res.status(200).send(buf);
  } catch (err) {
    console.error('[memory-api] tts error', err);
    res.status(500).json({ ok: false, error: 'tts_failed', detail: String(err).slice(0, 400) });
  }
});

app.post('/v1/memory/conversations', auth, async (req, res) => {
  try {
    if (!ensureMemoryStore(res)) return;

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
    if (!ensureMemoryStore(res)) return;

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
    if (!ensureMemoryStore(res)) return;

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

await initMongo();
setInterval(() => pruneAsrSessions(), 60 * 1000).unref();

app.listen(Number(PORT), () => {
  console.log(`[memory-api] listening on :${PORT}`);
});
