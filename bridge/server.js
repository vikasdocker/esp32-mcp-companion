'use strict';

/*
 * ESP32 OpenCode Bridge
 * ---------------------
 * Receives chat messages from the ESP32, asks OpenCode for a reply,
 * and returns plain JSON. No npm dependencies.
 *
 * The bridge is also the public rendezvous point. The board registers its
 * LAN address here every few minutes; the landing page uses that to send a
 * phone that is on the same Wi-Fi to the board's own chat page. That way the
 * board can travel and join any network without anyone memorising its IP.
 *
 * Built by BlueFluteX - https://blueflutex.in/
 */

const http = require('http');
const https = require('https');
const { execFile } = require('child_process');
const fs = require('fs');
const path = require('path');

const CONFIG_PATH = path.join(__dirname, 'config.json');

let config;
try {
  config = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
} catch (error) {
  console.error(`[bridge] Cannot read ${CONFIG_PATH}: ${error.message}`);
  process.exit(1);
}

const PORT = Number(process.env.PORT || config.port || 8787);
const TOKEN = process.env.BRIDGE_TOKEN || config.token || '';
const OPENCODE_EXE =
  process.env.OPENCODE_EXE ||
  config.opencodeExe ||
  'C:\\Users\\vikas\\AppData\\Roaming\\npm\\node_modules\\@opencode\\cli\\bin\\opencode.exe';
const TIMEOUT_MS = Number(config.timeoutMs || 120000);
const DEVICE_CONTROL = (config.deviceControl && config.deviceControl.enabled)
  ? config.deviceControl
  : null;
const PRIMARY = config.model || { providerID: 'ollama', id: 'qwen2.5:7b' };
const FALLBACKS = Array.isArray(config.fallbackModels) ? config.fallbackModels : [];
const MAX_BODY_BYTES = 256 * 1024;
const MAX_PROMPT_CHARS = 24000;
const COOLDOWN_DEFAULT_MS = 10 * 60 * 1000;
const COOLDOWN_MAX_MS = 24 * 60 * 60 * 1000;
const startedAt = Date.now();

// Public-facing abuse controls. The bridge is reachable from the internet once
// the Cloudflare tunnel is up, so every request costs someone real CPU.
const RATE_WINDOW_MS = Number((config.rateLimit && config.rateLimit.windowMs) || 5 * 60 * 1000);
const RATE_MAX = Number((config.rateLimit && config.rateLimit.max) || 40);
const DEVICE_TTL_MS = 20 * 60 * 1000;

// Models that recently returned a rate-limit error are skipped until their
// cooldown expires, so every chat message does not pay for a failed attempt.
const modelCooldowns = new Map();
let lastUsedModel = null;

// Last known LAN address of the board, refreshed by POST /register.
let device = null;

// Only one generation at a time; the board sends one message at a time anyway
// and Ollama thrashes if two prompts arrive together.
let chatInFlight = false;

const rateBuckets = new Map();

function modelKey(model) {
  return `${model.providerID}/${model.id}`;
}

function cooldownRemainingMs(model) {
  const until = modelCooldowns.get(modelKey(model));
  return until && until > Date.now() ? until - Date.now() : 0;
}

function setCooldown(model, error) {
  const retryAfter = Number(error.retryAfterMs) || 0;
  const duration = Math.min(
    Math.max(retryAfter || COOLDOWN_DEFAULT_MS, 60 * 1000),
    COOLDOWN_MAX_MS
  );
  modelCooldowns.set(modelKey(model), Date.now() + duration);
  return duration;
}

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

function sendJson(res, status, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
    'Access-Control-Allow-Origin': '*',
  });
  res.end(body);
}

function sendHtml(res, status, body) {
  res.writeHead(status, {
    'Content-Type': 'text/html; charset=utf-8',
    'Content-Length': Buffer.byteLength(body),
    'Cache-Control': 'no-store',
    'X-Robots-Tag': 'noindex, nofollow',
  });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let size = 0;
    const chunks = [];
    req.on('data', (chunk) => {
      size += chunk.length;
      if (size > MAX_BODY_BYTES) {
        reject(Object.assign(new Error('Request body too large'), { statusCode: 413 }));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

// ---------------------------------------------------------------------------
// Public page + device registry
// ---------------------------------------------------------------------------

function escapeHtml(value) {
  return String(value == null ? '' : value).replace(/[&<>"']/g, (character) => {
    switch (character) {
      case '&': return '&amp;';
      case '<': return '&lt;';
      case '>': return '&gt;';
      case '"': return '&quot;';
      default: return '&#39;';
    }
  });
}

function isPrivateIPv4(value) {
  const parts = String(value || '').split('.');
  if (parts.length !== 4) return false;
  const octets = parts.map((part) => (/^\d{1,3}$/.test(part) ? Number(part) : NaN));
  if (octets.some((octet) => Number.isNaN(octet) || octet > 255)) return false;
  const [a, b] = octets;
  if (a === 10) return true;
  if (a === 172 && b >= 16 && b <= 31) return true;
  if (a === 192 && b === 168) return true;
  if (a === 169 && b === 254) return true;
  return false;
}

function deviceTarget() {
  if (!device) return null;
  if (Date.now() - device.at > DEVICE_TTL_MS) return null;
  return `http://${device.ip}:${device.port}/`;
}

function ageLabel(ms) {
  const seconds = Math.max(0, Math.round(ms / 1000));
  if (seconds < 90) return `${seconds}s ago`;
  const minutes = Math.round(seconds / 60);
  if (minutes < 90) return `${minutes} min ago`;
  return `${Math.round(minutes / 60)} h ago`;
}

function landingPage() {
  const target = deviceTarget();
  const fresh = Boolean(target);
  const meta = device
    ? `${escapeHtml(device.ssid || 'unknown network')} · signal ${escapeHtml(device.rssi)} dBm · seen ${ageLabel(Date.now() - device.at)}`
    : 'The board has never checked in on this bridge.';

  const action = fresh
    ? `<a class="btn" href="${escapeHtml(target)}">Open chat</a>
       <p class="hint">Sending you to the board&hellip;</p>
       <script>setTimeout(function () { location.replace(${JSON.stringify(target)}); }, 900);</script>`
    : `<div class="steps">
         <p class="lead">The board is not reachable right now.</p>
         <ol>
           <li>Power the board. Its LED blinks while it joins Wi-Fi.</li>
           <li>Put this phone on the <strong>same Wi-Fi</strong> as the board.</li>
           <li>Reload this page.</li>
         </ol>
       </div>`;

  return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="robots" content="noindex,nofollow">
<title>ESP32 Chat</title>
<style>
:root{--bg:#070b10;--panel:#0e151d;--line:#1b2733;--text:#e8eef5;--muted:#8a99a8;--accent:#2dd4bf;--accent2:#38bdf8}
*{box-sizing:border-box}
body{margin:0;min-height:100vh;display:grid;place-items:center;padding:24px;color:var(--text);
background:radial-gradient(900px 500px at 15% -10%,rgba(45,212,191,.12),transparent 60%),
radial-gradient(700px 400px at 100% 0%,rgba(56,189,248,.10),transparent 55%),var(--bg);
font:15px/1.6 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
.card{width:100%;max-width:420px;background:linear-gradient(180deg,#101a24,#0b131b);border:1px solid var(--line);
border-radius:22px;padding:28px 24px;box-shadow:0 24px 60px rgba(0,0,0,.45);text-align:center}
.logo{width:52px;height:52px;margin:0 auto 16px;border-radius:16px;display:grid;place-items:center;font-size:26px;color:#04121a;
background:linear-gradient(135deg,var(--accent),var(--accent2));box-shadow:0 10px 26px rgba(45,212,191,.28)}
h1{margin:0 0 6px;font-size:19px;letter-spacing:.2px}
.sub{margin:0 0 22px;color:var(--muted);font-size:13px}
.lead{margin:0 0 18px;font-size:15px}
.btn{display:block;padding:14px 18px;border-radius:14px;text-decoration:none;font-weight:600;color:#04121a;
background:linear-gradient(135deg,var(--accent),var(--accent2));box-shadow:0 10px 26px rgba(45,212,191,.25)}
.btn:active{transform:scale(.985)}
.hint{margin:14px 0 0;color:var(--muted);font-size:12.5px}
.steps{text-align:left;color:#c3d0dc;font-size:14px}
.steps ol{margin:0;padding-left:20px}
.steps li{margin:0 0 8px}
.meta{margin:22px 0 0;padding-top:16px;border-top:1px solid var(--line);color:#5f6f7e;font-size:11.5px;word-break:break-word}
.foot{margin:10px 0 0;color:#455463;font-size:11px}
.foot a{color:#5f6f7e;text-decoration:none}
</style>
</head>
<body>
<main class="card">
  <div class="logo">&#9672;</div>
  <h1>ESP32 Chat</h1>
  <p class="sub">Your desk robot, bridged to OpenCode</p>
  ${action}
  <p class="meta">${meta}</p>
  <p class="foot">Built by <a href="https://blueflutex.in/">BlueFluteX</a></p>
</main>
</body>
</html>`;
}

// ---------------------------------------------------------------------------
// Prompt + model plumbing
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Device control
// ---------------------------------------------------------------------------
// The model is told about the board's outputs and ends its reply with an
// ACTION: line. The bridge strips that line, carries the action out on the
// real board, and appends the new state to the next prompt.

const ACTION_RE = /^ACTION:\s*(\{.*\})\s*$/im;

async function fetchDeviceOutputs() {
  const target = deviceTarget();
  if (!target || !DEVICE_CONTROL) return null;

  try {
    const res = await httpGet(target + 'api/outputs');
    if (!res.ok) return null;
    return JSON.parse(res.body);
  } catch (error) {
    log(`[device] could not read outputs: ${error.message}`);
    return null;
  }
}

function parseActions(text) {
  const actions = [];
  let match;
  ACTION_RE.lastIndex = 0;
  while ((match = ACTION_RE.exec(text)) !== null) {
    try {
      const action = JSON.parse(match[1]);
      if (action && typeof action.output === 'string' && typeof action.value === 'number') {
        actions.push({ output: String(action.output), value: Number(action.value) });
      }
    } catch (error) {
      log(`[device] malformed ACTION line: ${match[1]}`);
    }
  }
  return actions;
}

function stripActions(text) {
  return text.replace(ACTION_RE, '').replace(/\n{2,}/g, '\n').trim();
}

async function executeActions(actions, state) {
  const target = deviceTarget();
  if (!target || !actions.length) return state;

  for (const action of actions) {
    try {
      const res = await httpPost(
        target + 'api/action',
        JSON.stringify({ output: action.output, value: action.value })
      );
      if (!res.ok) {
        log(`[device] action ${action.output}=${action.value} failed HTTP ${res.status}`);
        continue;
      }
      const data = JSON.parse(res.body);
      if (Array.isArray(data.state)) state = data.state;
      log(`[device] ${action.output}=${action.value} -> ${JSON.stringify(data.state || {})}`);
    } catch (error) {
      log(`[device] action ${action.output}=${action.value} failed: ${error.message}`);
    }
  }
  return state;
}

function httpGet(url) {
  return new Promise((resolve, reject) => {
    const client = url.startsWith('https://')
      ? https
      : http;
    const req = client.get(url, { timeout: 8000 }, (res) => {
      let data = '';
      res.on('data', (chunk) => { data += chunk; });
      res.on('end', () => resolve({ ok: res.statusCode >= 200 && res.statusCode < 300, status: res.statusCode, body: data }));
    });
    req.on('error', reject);
    req.on('timeout', () => { req.destroy(); reject(new Error('timeout')); });
  });
}

function httpPost(url, body) {
  return new Promise((resolve, reject) => {
    const client = url.startsWith('https://')
      ? https
      : http;
    const req = client.request(url, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) },
      timeout: 8000,
    }, (res) => {
      let data = '';
      res.on('data', (chunk) => { data += chunk; });
      res.on('end', () => resolve({ ok: res.statusCode >= 200 && res.statusCode < 300, status: res.statusCode, body: data }));
    });
    req.on('error', reject);
    req.on('timeout', () => { req.destroy(); reject(new Error('timeout')); });
    req.write(body);
    req.end();
  });
}

function validateMessages(value) {
  if (!Array.isArray(value) || value.length === 0) {
    throw Object.assign(new Error('messages must be a non-empty array'), { statusCode: 400 });
  }

  const cleaned = [];
  for (const message of value.slice(-20)) {
    if (!message || typeof message !== 'object') continue;
    const role = message.role === 'user' ? 'user' : message.role === 'assistant' ? 'assistant' : null;
    const content = typeof message.content === 'string' ? message.content.trim() : '';
    if (!role || !content) continue;
    if (content.length > 4000) {
      throw Object.assign(new Error('Each message must be 4000 characters or less'), { statusCode: 400 });
    }
    cleaned.push({ role, content });
  }

  if (!cleaned.length) {
    throw Object.assign(new Error('No valid messages'), { statusCode: 400 });
  }
  if (cleaned[cleaned.length - 1].role !== 'user') {
    throw Object.assign(new Error('The last message must be from the user'), { statusCode: 400 });
  }
  return cleaned;
}

function buildPrompt(messages, deviceInfo) {
  const system = [
    'You are a friendly, helpful AI assistant living on a small ESP32 desk device.',
    'You are chatting through a tiny web page, so keep replies plain text: no markdown, no code fences, no tables.',
    'Be conversational and concise: usually 1-4 sentences, and never more than 120 words unless the user explicitly asks for detail.',
    'You cannot browse the internet, read files, run code, or use tools. If asked to do those things, say you cannot from this device.',
    'If the user writes in another language, reply in that language.',
  ];

  if (deviceInfo && deviceInfo.outputs && deviceInfo.outputs.length) {
    system.push(
      'The device has controllable outputs: ' + JSON.stringify(deviceInfo.outputs) + '.',
      DEVICE_CONTROL && DEVICE_CONTROL.prompt ? DEVICE_CONTROL.prompt : ''
    );
  }

  const lines = [];
  let used = system.join(' ').length + 12;
  for (let index = messages.length - 1; index >= 0; index -= 1) {
    const message = messages[index];
    const line = `${message.role === 'user' ? 'User' : 'Assistant'}: ${message.content}`;
    if (used + line.length + 1 > MAX_PROMPT_CHARS) break;
    lines.unshift(line);
    used += line.length + 1;
  }

  return [system.join(' '), '', ...lines, 'Assistant:'].join('\n');
}

function callOpenCode(prompt, model) {
  return new Promise((resolve, reject) => {
    const body = { prompt };
    if (model && model.providerID && model.id) {
      body.model = { providerID: model.providerID, id: model.id };
    }

    const args = ['api', 'post', '/api/experimental/generate/text', '--data', JSON.stringify(body)];
    const child = execFile(
      OPENCODE_EXE,
      args,
      { timeout: TIMEOUT_MS, maxBuffer: 20 * 1024 * 1024, windowsHide: true },
      (error, stdout, stderr) => {
        const raw = (stdout || '').trim();
        let parsed = null;

        if (raw) {
          try {
            parsed = JSON.parse(raw);
          } catch {
            const first = raw.indexOf('{');
            const last = raw.lastIndexOf('}');
            if (first >= 0 && last > first) {
              try {
                parsed = JSON.parse(raw.slice(first, last + 1));
              } catch {
                /* keep parsed null */
              }
            }
          }
        }

        if (parsed && parsed.data && typeof parsed.data.text === 'string') {
          resolve({ text: parsed.data.text.trim(), model });
          return;
        }

        const message = parsed
          ? parsed.message || parsed._tag || JSON.stringify(parsed)
          : error
            ? error.message
            : 'OpenCode returned no output';

        const failure = new Error(message);
        failure.statusCode = 502;
        failure.retryAfterMs = parsed ? Number(parsed.retryAfterMs) || 0 : 0;
        failure.detail = { stderr: (stderr || '').trim(), raw: raw.slice(0, 2000) };
        reject(failure);
      }
    );

    child.on('error', reject);
  });
}

async function generate(prompt) {
  const chain = [PRIMARY, ...FALLBACKS];
  let lastError = null;

  for (const model of chain) {
    const key = modelKey(model);
    const remaining = cooldownRemainingMs(model);
    if (remaining > 0) {
      log(`[model] skip ${key} (cooling down ${Math.ceil(remaining / 1000)}s)`);
      continue;
    }

    try {
      log(`[model] trying ${key}`);
      const result = await callOpenCode(prompt, model);
      log(`[model] ok ${key} (${result.text.length} chars)`);
      lastUsedModel = key;
      return result;
    } catch (error) {
      lastError = error;
      log(`[model] failed ${key}: ${error.message}`);

      if (/rate.?limit|free usage limit|quota|429/i.test(error.message) || error.retryAfterMs) {
        const duration = setCooldown(model, error);
        log(`[model] ${key} cooling down for ${Math.ceil(duration / 60000)} min`);
      }
    }
  }

  throw lastError || new Error('No model configured');
}

// ---------------------------------------------------------------------------
// HTTP routing
// ---------------------------------------------------------------------------

function clientIp(req) {
  const forwarded = req.headers['cf-connecting-ip'] || req.headers['x-forwarded-for'];
  if (forwarded) return String(forwarded).split(',')[0].trim();
  return req.socket.remoteAddress || 'unknown';
}

function allowRequest(key) {
  const now = Date.now();
  const bucket = rateBuckets.get(key);
  if (!bucket || now > bucket.reset) {
    rateBuckets.set(key, { count: 1, reset: now + RATE_WINDOW_MS });
    return true;
  }
  if (bucket.count >= RATE_MAX) return false;
  bucket.count += 1;
  return true;
}

function pruneRateBuckets() {
  const now = Date.now();
  for (const [key, bucket] of rateBuckets) {
    if (now > bucket.reset) rateBuckets.delete(key);
  }
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || 'localhost'}`);

  if (req.method === 'OPTIONS') {
    res.writeHead(204, {
      'Access-Control-Allow-Origin': '*',
      'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
      'Access-Control-Allow-Headers': 'Content-Type, X-Bridge-Token',
    });
    res.end();
    return;
  }

  if (req.method === 'GET' && url.pathname === '/favicon.ico') {
    res.writeHead(204);
    res.end();
    return;
  }

  if (req.method === 'GET' && url.pathname === '/') {
    sendHtml(res, 200, landingPage());
    return;
  }

  if (req.method === 'GET' && url.pathname === '/health') {
    const now = Date.now();
    sendJson(res, 200, {
      ok: true,
      service: 'esp32-opencode-bridge',
      uptime: Math.floor((Date.now() - startedAt) / 1000),
      model: modelKey(PRIMARY),
      fallbackModels: FALLBACKS.map(modelKey),
      lastUsedModel,
      cooldowns: [...modelCooldowns.entries()]
        .filter(([, until]) => until > now)
        .map(([model, until]) => ({ model, secondsRemaining: Math.ceil((until - now) / 1000) })),
      device: device
        ? {
            ip: device.ip,
            port: device.port,
            ssid: device.ssid,
            rssi: device.rssi,
            firmware: device.firmware,
            ageSeconds: Math.floor((now - device.at) / 1000),
            reachable: Boolean(deviceTarget()),
          }
        : null,
    });
    return;
  }

  if (req.method === 'GET' && url.pathname === '/where') {
    sendJson(res, 200, {
      registered: Boolean(device),
      reachable: Boolean(deviceTarget()),
      target: deviceTarget(),
      device: device
        ? { ...device, ageSeconds: Math.floor((Date.now() - device.at) / 1000) }
        : null,
    });
    return;
  }

  if (req.method === 'POST' && url.pathname === '/register') {
    if (TOKEN && req.headers['x-bridge-token'] !== TOKEN) {
      sendJson(res, 401, { error: 'Invalid bridge token' });
      return;
    }

    try {
      const payload = JSON.parse(await readBody(req));
      const ip = String(payload.ip || '');
      const port = Number(payload.port) || 80;

      if (!isPrivateIPv4(ip)) {
        sendJson(res, 400, { error: 'ip must be a private IPv4 address' });
        return;
      }
      if (!Number.isInteger(port) || port < 1 || port > 65535) {
        sendJson(res, 400, { error: 'port out of range' });
        return;
      }

      const changed = !device || device.ip !== ip || device.port !== port;
      device = {
        ip,
        port,
        ssid: String(payload.ssid || '').slice(0, 64),
        rssi: Number(payload.rssi) || 0,
        firmware: String(payload.firmware || '').slice(0, 32),
        at: Date.now(),
      };

      if (changed) log(`[device] board at http://${ip}:${port}/ on "${device.ssid}"`);
      sendJson(res, 200, { ok: true, target: deviceTarget() });
    } catch (error) {
      sendJson(res, error.statusCode || 400, { error: error.message });
    }
    return;
  }

  if (req.method === 'POST' && url.pathname === '/chat') {
    if (TOKEN && req.headers['x-bridge-token'] !== TOKEN) {
      sendJson(res, 401, { error: 'Invalid bridge token' });
      return;
    }

    const ip = clientIp(req);
    if (!allowRequest(ip)) {
      log(`[chat] rate limited ${ip}`);
      sendJson(res, 429, { error: 'Too many messages. Wait a few minutes and try again.' });
      return;
    }

    if (chatInFlight) {
      sendJson(res, 429, { error: 'Still thinking about the last message. Try again in a moment.' });
      return;
    }

    try {
      const raw = await readBody(req);
      let payload;
      try {
        payload = JSON.parse(raw);
      } catch {
        sendJson(res, 400, { error: 'Invalid JSON body' });
        return;
      }

      const messages = validateMessages(payload.messages);

      // Tell the model what the board can do, then let it act on it.
      let deviceInfo = null;
      if (DEVICE_CONTROL) {
        deviceInfo = await fetchDeviceOutputs();
        if (deviceInfo) log(`[device] ${deviceInfo.length} outputs advertised`);
      }

      const prompt = buildPrompt(messages, deviceInfo);
      log(`[chat] ${messages.length} messages, ${prompt.length} prompt chars from ${ip}`);

      chatInFlight = true;
      let result;
      try {
        result = await generate(prompt);
      } finally {
        chatInFlight = false;
      }

      // The model asked the board to do something. Carry it out and feed the
      // new state back so the next message reflects reality.
      let reply = result.text;
      let deviceState = null;
      if (DEVICE_CONTROL) {
        const actions = parseActions(reply);
        if (actions.length) {
          log(`[device] ${actions.length} action(s) requested`);
          reply = stripActions(reply);
          if (!reply) reply = 'Done.';
          deviceState = await executeActions(actions, deviceInfo);
        }
      }

      const response = {
        reply,
        model: `${result.model.providerID}/${result.model.id}`,
      };
      if (deviceState) response.device = deviceState;
      sendJson(res, 200, response);
    } catch (error) {
      const status = error.statusCode || 500;
      log(`[chat] error ${status}: ${error.message}`);
      sendJson(res, status, { error: error.message });
    }
    return;
  }

  sendJson(res, 404, { error: 'Not found' });
});

server.listen(PORT, '0.0.0.0', () => {
  log(`[bridge] listening on http://0.0.0.0:${PORT}`);
  log(`[bridge] model chain: ${[PRIMARY, ...FALLBACKS].map(modelKey).join(' -> ')}`);
  log(`[bridge] opencode: ${OPENCODE_EXE}`);
  log(`[bridge] public landing page: ${config.publicUrl || '(set publicUrl in config.json)'}`);

  setInterval(pruneRateBuckets, 60 * 1000).unref();

  // Warm up the preferred model so the first chat reply is fast.
  setTimeout(() => {
    log('[warmup] loading model...');
    generate('Reply with exactly: OK')
      .then(() => log('[warmup] model ready'))
      .catch((error) => log(`[warmup] skipped: ${error.message}`));
  }, 1500);
});

process.on('SIGINT', () => {
  log('[bridge] shutting down');
  server.close(() => process.exit(0));
});
process.on('SIGTERM', () => {
  log('[bridge] shutting down');
  server.close(() => process.exit(0));
});
