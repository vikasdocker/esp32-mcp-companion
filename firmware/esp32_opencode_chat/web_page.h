/*
 * ESP32 OpenCode Chat - embedded chat page.
 *
 * This lives in its own header instead of the .ino on purpose: the Arduino
 * sketch preprocessor inserts #line directives and function prototypes into
 * raw string literals, which corrupts the inline JavaScript below.
 *
 * Built by BlueFluteX - https://blueflutex.in/
 */

#ifndef WEB_PAGE_H
#define WEB_PAGE_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"=====(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#070b10">
<title>ESP32 Chat</title>
<style>
:root{--bg:#070b10;--panel:#0e151d;--line:#1b2733;--text:#e8eef5;--muted:#8a99a8;--accent:#2dd4bf;--accent2:#38bdf8;--user1:#1d4ed8;--user2:#1e3a8a;--danger:#fb7185}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{height:100%}
body{margin:0;color:var(--text);background:radial-gradient(900px 500px at 15% -10%,rgba(45,212,191,.12),transparent 60%),radial-gradient(700px 400px at 100% 0%,rgba(56,189,248,.10),transparent 55%),var(--bg);font:15px/1.55 ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;display:flex;flex-direction:column;height:100vh;height:100dvh;overflow:hidden}
header{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:14px 16px;padding-top:calc(14px + env(safe-area-inset-top));background:rgba(7,11,16,.85);backdrop-filter:blur(14px);border-bottom:1px solid var(--line);z-index:10}
.brand{display:flex;align-items:center;gap:11px;min-width:0}
.logo{width:38px;height:38px;border-radius:12px;display:grid;place-items:center;font-size:19px;color:#04121a;background:linear-gradient(135deg,var(--accent),var(--accent2));box-shadow:0 6px 18px rgba(45,212,191,.25);flex:0 0 auto}
.titles{min-width:0}
h1{margin:0;font-size:15px;letter-spacing:.2px}
.titles p{margin:0;font-size:11.5px;color:var(--muted);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:52vw}
.status{display:flex;align-items:center;gap:7px;font-size:12px;color:var(--muted);border:1px solid var(--line);padding:6px 10px;border-radius:999px;background:#0b131b;flex:0 0 auto}
.dot{width:8px;height:8px;border-radius:50%;background:#f59e0b;box-shadow:0 0 0 3px rgba(245,158,11,.15)}
.dot.ok{background:#34d399;box-shadow:0 0 0 3px rgba(52,211,153,.15)}
.dot.bad{background:var(--danger);box-shadow:0 0 0 3px rgba(251,113,133,.15)}
#chat{flex:1;min-height:0;overflow-y:auto;padding:18px 14px 6px;display:flex;flex-direction:column;gap:10px;scroll-behavior:smooth}
.msg{display:flex;max-width:88%;animation:pop .18s ease-out}
.msg.user{align-self:flex-end;justify-content:flex-end}
.msg.bot{align-self:flex-start}
.bubble{padding:11px 14px;border-radius:16px;white-space:pre-wrap;overflow-wrap:anywhere;font-size:15px}
.msg.user .bubble{background:linear-gradient(135deg,var(--user1),var(--user2));border-bottom-right-radius:5px;color:#fff;box-shadow:0 6px 20px rgba(29,78,216,.25)}
.msg.bot .bubble{background:linear-gradient(180deg,#131d28,#101822);border:1px solid var(--line);border-bottom-left-radius:5px}
@keyframes pop{from{opacity:0;transform:translateY(6px) scale(.98)}to{opacity:1;transform:none}}
.typing{display:flex;gap:5px;align-items:center;padding:0 22px 8px;height:26px}
.typing.hidden{visibility:hidden}
.typing span{width:7px;height:7px;border-radius:50%;background:var(--accent);opacity:.4;animation:bounce 1.2s infinite}
.typing span:nth-child(2){animation-delay:.15s}
.typing span:nth-child(3){animation-delay:.3s}
@keyframes bounce{0%,60%,100%{transform:translateY(0);opacity:.35}30%{transform:translateY(-4px);opacity:1}}
footer{padding:8px 14px;padding-bottom:calc(14px + env(safe-area-inset-bottom));background:linear-gradient(180deg,transparent,rgba(7,11,16,.92) 35%)}
.chips{display:flex;gap:8px;overflow-x:auto;padding:2px 2px 10px;scrollbar-width:none}
.chips::-webkit-scrollbar{display:none}
.chips button{flex:0 0 auto;background:#0f1a24;border:1px solid var(--line);color:#b9c7d5;border-radius:999px;padding:7px 12px;font-size:12.5px;cursor:pointer}
.chips button:active{transform:scale(.97)}
form{display:flex;gap:10px;align-items:flex-end;background:var(--panel);border:1px solid var(--line);border-radius:18px;padding:8px 8px 8px 14px;box-shadow:0 10px 30px rgba(0,0,0,.35)}
textarea{flex:1;background:transparent;border:0;outline:0;color:var(--text);font:inherit;font-size:16px;resize:none;max-height:120px;padding:6px 0;line-height:1.4}
textarea::placeholder{color:#6b7a89}
#send{width:42px;height:42px;border:0;border-radius:14px;background:linear-gradient(135deg,var(--accent),var(--accent2));color:#04121a;font-size:18px;font-weight:700;cursor:pointer;display:grid;place-items:center;flex:0 0 auto;box-shadow:0 6px 18px rgba(45,212,191,.3)}
#send:disabled{opacity:.45;cursor:not-allowed;box-shadow:none}
.foot{display:flex;justify-content:space-between;align-items:center;padding:8px 4px 0;color:#5f6f7e;font-size:11.5px;gap:10px}
.foot button{background:none;border:0;color:#5f6f7e;font:inherit;cursor:pointer;text-decoration:underline;text-underline-offset:3px;flex:0 0 auto}
@media(max-width:390px){#hint{display:none}}
.device{margin:12px 16px 0;padding:12px 14px;border:1px solid var(--line);border-radius:14px;background:#0d151e;display:none;flex-direction:column;gap:10px}
.device.show{display:flex}
.d-head{display:flex;align-items:center;justify-content:space-between;gap:8px}
.d-title{font-size:11px;letter-spacing:.6px;text-transform:uppercase;color:var(--muted)}
.d-state{font-size:11px;color:var(--accent)}
.d-outputs{display:flex;flex-direction:column;gap:8px}
.d-out{display:flex;align-items:center;justify-content:space-between;gap:10px;background:#0a1219;border:1px solid var(--line);border-radius:10px;padding:8px 10px}
.d-name{font-size:13px;flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.d-ctrl{display:flex;gap:6px}
.d-ctrl button{width:34px;height:30px;border:0;border-radius:8px;background:#0f1a24;border:1px solid var(--line);color:#b9c7d5;font-size:13px;cursor:pointer}
.d-ctrl button.on{background:linear-gradient(135deg,var(--accent),var(--accent2));color:#04121a;font-weight:700}
.d-ctrl button.dim{flex:1;background:#0f1a24;border:1px solid var(--line);color:#b9c7d5;border-radius:8px;font-size:12px;cursor:pointer}
.d-ctrl button.dim:active{transform:scale(.97)}
.d-bar{width:60px;height:6px;border-radius:3px;background:#0a1219;overflow:hidden}
.d-bar i{display:block;height:100%;background:linear-gradient(90deg,var(--accent),var(--accent2))}
</style>
</head>
<body>
<header>
  <div class="brand">
    <div class="logo">&#9672;</div>
    <div class="titles">
      <h1>ESP32 Chat</h1>
      <p id="subtitle">OpenCode &middot; via the internet</p>
    </div>
  </div>
  <div class="status"><span id="dot" class="dot"></span><span id="statusText">connecting</span></div>
</header>
<div id="device" class="device hidden">
  <div class="d-head"><span class="d-title">Device</span><span id="d-state" class="d-state"></span></div>
  <div id="d-outputs" class="d-outputs"></div>
</div>
<main id="chat" aria-live="polite"></main>
<div id="typing" class="typing hidden" aria-hidden="true"><span></span><span></span><span></span></div>
<footer>
  <div class="chips" id="chips">
    <button type="button">Tell me a joke</button>
    <button type="button">What can you do?</button>
    <button type="button">Who made you?</button>
  </div>
  <form id="form" autocomplete="off">
    <textarea id="input" rows="1" maxlength="2000" placeholder="Ask me anything..." enterkeyhint="send"></textarea>
    <button id="send" type="submit" aria-label="Send message">&#10148;</button>
  </form>
  <div class="foot"><span id="hint">Enter to send &middot; Shift+Enter for a new line</span><button id="clear" type="button">Clear chat</button></div>
</footer>
<script>
(function () {
  'use strict';
  var chat = document.getElementById('chat');
  var form = document.getElementById('form');
  var input = document.getElementById('input');
  var sendBtn = document.getElementById('send');
  var typing = document.getElementById('typing');
  var dot = document.getElementById('dot');
  var statusText = document.getElementById('statusText');
  var subtitle = document.getElementById('subtitle');
  var chips = document.getElementById('chips');
  var clearBtn = document.getElementById('clear');

  var WELCOME = "Hi! I'm your ESP32 chatbot. I ride along on whatever Wi-Fi you give me and do my thinking on your PC through OpenCode. Ask me anything.";
  var STORAGE_KEY = 'esp32chat.v1';
  var messages = [];
  var busy = false;

  function load() {
    try {
      var raw = sessionStorage.getItem(STORAGE_KEY);
      if (raw) messages = JSON.parse(raw) || [];
    } catch (e) { messages = []; }
    if (messages.length) {
      messages.forEach(function (m) { render(m.role, m.content); });
    } else {
      render('assistant', WELCOME);
    }
  }

  function save() {
    try { sessionStorage.setItem(STORAGE_KEY, JSON.stringify(messages.slice(-40))); } catch (e) {}
  }

  function render(role, text) {
    var row = document.createElement('div');
    row.className = 'msg ' + (role === 'user' ? 'user' : 'bot');
    var bubble = document.createElement('div');
    bubble.className = 'bubble';
    bubble.textContent = text;
    row.appendChild(bubble);
    chat.appendChild(row);
    chat.scrollTop = chat.scrollHeight;
  }

  function add(role, text, remember) {
    render(role, text);
    if (remember !== false) {
      messages.push({ role: role, content: text });
      save();
    }
  }

  function setBusy(value) {
    busy = value;
    sendBtn.disabled = value;
    typing.classList.toggle('hidden', !value);
  }

  function send(text) {
    text = (text || '').trim();
    if (!text || busy) return;
    add('user', text);
    input.value = '';
    input.style.height = 'auto';
    setBusy(true);
    chat.scrollTop = chat.scrollHeight;

    var controller = new AbortController();
    var timer = setTimeout(function () { controller.abort(); }, 200000);

    fetch('/api/chat', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ messages: messages.slice(-16) }),
      signal: controller.signal
    }).then(function (res) {
      return res.json().catch(function () { return {}; }).then(function (data) {
        if (!res.ok || !data.reply) {
          throw new Error(data.error || ('Request failed (HTTP ' + res.status + ')'));
        }
        return data;
      });
    }).then(function (data) {
      add('assistant', data.reply);
      if (data.model) subtitle.textContent = String(data.model).replace('ollama/', '') + ' \u00b7 via OpenCode';
      if (data.device) renderDevice(data.device);
    }).catch(function (err) {
      var msg = err && err.name === 'AbortError'
        ? 'That took too long. The model may still be loading - try again in a moment.'
        : (err && err.message ? err.message : 'Something went wrong.');
      add('assistant', '\u26a0 ' + msg, false);
    }).finally(function () {
      clearTimeout(timer);
      setBusy(false);
      input.focus();
    });
  }

  form.addEventListener('submit', function (e) {
    e.preventDefault();
    send(input.value);
  });

  input.addEventListener('keydown', function (e) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      send(input.value);
    }
  });

  input.addEventListener('input', function () {
    input.style.height = 'auto';
    input.style.height = Math.min(input.scrollHeight, 120) + 'px';
  });

  chips.addEventListener('click', function (e) {
    var btn = e.target.closest('button');
    if (btn) send(btn.textContent);
  });

  clearBtn.addEventListener('click', function () {
    messages = [];
    try { sessionStorage.removeItem(STORAGE_KEY); } catch (e) {}
    chat.innerHTML = '';
    render('assistant', WELCOME);
    input.focus();
  });

  function loadDevice() {
  fetch('/api/outputs', { cache: 'no-store' }).then(function (r) {
    if (!r.ok) throw new Error('bad');
    return r.json();
  }).then(function (outputs) {
    lastOutputs = outputs;
    renderDevice(outputs);
  }).catch(function () {});
}

function renderDevice(outputs) {
  var panel = document.getElementById('device');
  var list = document.getElementById('d-outputs');
  var state = document.getElementById('d-state');
  if (!outputs.length) {
    panel.classList.remove('show');
    return;
  }
  panel.classList.add('show');
  list.innerHTML = '';
  outputs.forEach(function (o) {
    var row = document.createElement('div');
    row.className = 'd-out';
    var name = document.createElement('div');
    name.className = 'd-name';
    name.textContent = o.name;
    var ctrl = document.createElement('div');
    ctrl.className = 'd-ctrl';
    var on = document.createElement('button');
    on.textContent = 'On';
    on.className = 'on';
    on.addEventListener('click', function () { setOutput(o.id, 255); });
    var off = document.createElement('button');
    off.textContent = 'Off';
    off.addEventListener('click', function () { setOutput(o.id, 0); });
    var dim = document.createElement('button');
    dim.textContent = 'Dim';
    dim.className = 'dim';
    dim.addEventListener('click', function () { setOutput(o.id, o.value >= 128 ? 128 : 64); });
    var bar = document.createElement('div');
    bar.className = 'd-bar';
    bar.innerHTML = '<i style="width:' + (o.type === 'digital' ? (o.value ? '100%' : '0%') : (o.value / 255 * 100) + '%') + '"></i>';
    ctrl.appendChild(on);
    ctrl.appendChild(off);
    ctrl.appendChild(dim);
    row.appendChild(name);
    row.appendChild(bar);
    row.appendChild(ctrl);
    list.appendChild(row);
  });
  state.textContent = outputs.length + ' output' + (outputs.length === 1 ? '' : 's');
}

function setOutput(id, value) {
  fetch('/api/action', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ output: id, value: value })
  }).then(function (r) { return r.json(); }).then(function (data) {
    if (data.state) renderDevice(data.state);
  }).catch(function () {});
}

function ping() {
    fetch('/api/health', { cache: 'no-store' }).then(function (r) {
      if (!r.ok) throw new Error('bad');
      return r.json();
    }).then(function (data) {
      if (data.bridgeOk === false) {
        dot.className = 'dot';
        statusText.textContent = 'no bridge';
        subtitle.textContent = 'PC bridge unreachable';
      } else {
        dot.className = 'dot ok';
        statusText.textContent = 'online';
      }
    }).catch(function () {
      dot.className = 'dot bad';
      statusText.textContent = 'offline';
    });
  }

  load();
  ping();
  setInterval(ping, 30000);
  loadDevice();
  setInterval(loadDevice, 5000);
  input.focus();

  // The model can control the device, so tell it what exists on every message.
  var lastOutputs = [];
  function outputsForModel() {
    return lastOutputs.map(function (o) {
      return { id: o.id, name: o.name, type: o.type, value: o.value };
    });
  }
})();
</script>
</body>
</html>
)=====";

#endif  // WEB_PAGE_H
