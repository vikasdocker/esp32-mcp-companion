# ESP32 OpenCode Chat

A tiny AI chatbot that lives on an ESP32. The board is portable: it joins
whatever Wi-Fi is around — your phone hotspot, a home router, a friend's
network — and reaches the PC bridge over the internet through a Cloudflare
tunnel. One public URL is all your phone needs to find it.

```
Phone browser
        |
        |  1. open https://chat.blueflutex.in
        v
Cloudflare edge  --->  tunnel  --->  Node bridge on the PC
        |                                    |
        |  2. redirect to the board's         |  opencode api post
        |     address on the local Wi-Fi      v
        v                                 OpenCode -> Ollama / free model
ESP32  (chat page + /api/chat)
        |
        |  3. POST https://chat.blueflutex.in/chat
        +-----------------------------------------> bridge
```

No AI model runs on the ESP32. The board is the interface; the model runs on
the PC. OpenCode stays on localhost and is never exposed to the network — only
the bridge's small HTTP API is public, behind a token and a rate limit.

## Using it away from home

The board travels with you. You do not need the PC's hotspot, and you do not
need to know the board's IP.

1. Power the board. The LED blinks fast while it looks for a known network.
2. If it has never seen the network you are on, it broadcasts a setup hotspot
   called **ESP32-Chat-Setup** (password `blueflutex123`).
3. Join that hotspot from your phone. A setup page opens (or browse to
   <http://192.168.4.1>). Pick the local Wi-Fi, type its password, and save.
   The board reboots and joins that network.
4. Connect your phone to the **same Wi-Fi** as the board.
5. Open <https://chat.blueflutex.in/>. It looks up the board and redirects your
   phone to it.

The LED tells you where it is stuck:

| LED | Meaning |
| --- | --- |
| Fast blink | Joining Wi-Fi |
| Slow blink | Wi-Fi is fine, but the PC bridge is unreachable |
| Solid | Ready to chat |

To forget every saved network and start over, hold the **BOOT** button for
five seconds while the board is running.

Things that will not work: hotel or airport Wi-Fi with a login page, because
the board cannot click through a captive portal. Use your phone hotspot there
instead — the board joins it like any other network.

## Quick start (at home)

1. Keep the PC on and connected to Wi-Fi. Windows Mobile Hotspot needs an
   internet connection as its uplink.
2. The bridge starts automatically at login. If it is not running, double-click
   `bridge\start-bridge.cmd`.
3. Connect your phone to **ESP32-Chat** (password `blueflutex123`).
4. Open <http://esp32-chat.local/>, <http://192.168.137.135/>, or the public
   <https://chat.blueflutex.in/>.

The model chain is `opencode/deepseek-v4.1-flash-free` first, with
`ollama/qwen2.5:7b` as the automatic fallback. When the free cloud quota is
available the ESP32 uses it; when the quota is exhausted the bridge switches to
the local model without you doing anything. The first local reply after a boot
can take 30-60 seconds while the model loads into VRAM; later replies are
usually 1-3 seconds.

To remove the login auto-start, delete
`ESP32 OpenCode Bridge.lnk` from `shell:startup`.

## Project structure (animated)

![Animated project tree](docs/media/project-tree.svg)

## What you need

- ESP32 DevKit (CP2102 USB-to-UART), connected to **COM5**
- Windows PC with Node.js 18+ and OpenCode installed
- Ollama with a completion model, for example `qwen2.5:7b`
- Any 2.4 GHz Wi-Fi for the board, or a phone hotspot
- A Cloudflare account with the `blueflutex.in` zone, for the public tunnel

## First-time setup

### 1. Build toolchain

```powershell
winget install --id ArduinoSA.CLI -e
arduino-cli config init
arduino-cli config set board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

### 2. Local model

```powershell
winget install --id Ollama.Ollama -e
ollama pull qwen2.5:7b
```

OpenCode discovers Ollama automatically at `http://127.0.0.1:11434`.
Confirm it is visible:

```powershell
opencode models | Select-String ollama
```

### 3. Flash the firmware

Double-click `firmware\flash.cmd`, or run the commands manually:

```powershell
cd firmware\esp32_opencode_chat
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
arduino-cli upload -p COM5 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app .
arduino-cli monitor -p COM5 -c baudrate=115200
```

The `huge_app` partition gives the sketch 3 MB of flash. The serial monitor
prints the ESP32's IP address and the chat URL.

`flash.cmd` finds `arduino-cli` on your `PATH`, or falls back to
`C:\Program Files\Arduino CLI`. If you run the commands by hand and get
"not recognized", use the full path or add that folder to `PATH`.

The chat page lives in `firmware/esp32_opencode_chat/web_page.h`, not in the
`.ino`. Keep it there: the Arduino sketch preprocessor injects `#line`
directives and function prototypes into raw string literals, which corrupts
the embedded JavaScript and leaves the page blank.

### 4. Start the bridge

Double-click `bridge\start-bridge.cmd`, or:

```powershell
cd bridge
node server.js
```

The launcher also starts the **ESP32-Chat** 2.4 GHz hotspot if it is off, and
the Cloudflare tunnel. Keep the window open while using the device. A shortcut
in the Windows Startup folder runs this launcher automatically at login.

### 5. Set up the public tunnel (once)

```powershell
cd bridge
.\setup-tunnel.cmd
```

The first run opens a browser. Sign in to Cloudflare, pick the
`blueflutex.in` zone, and authorize. The script then creates a tunnel named
`esp32-chat`, routes `chat.blueflutex.in` to it, and writes
`%USERPROFILE%\.cloudflared\config.yml`.

Check it:

```powershell
Invoke-RestMethod https://chat.blueflutex.in/health
```

`cloudflared` is bundled at `bin\cloudflared.exe`. If it is missing, download
it from the Cloudflare releases page or run
`winget install Cloudflare.cloudflared`.

### 6. Chat

Connect your phone to the same Wi-Fi as the board and open
<https://chat.blueflutex.in/>. You can also open the board directly at
<http://esp32-chat.local/> or the IP printed in the serial monitor.

If mDNS is not available on your phone, find the current IP with:

```powershell
Get-NetNeighbor -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '192.168.137.*' }
```

## Configuration

| File | What it controls |
| --- | --- |
| `firmware/esp32_opencode_chat/secrets.h` | First-boot Wi-Fi, setup hotspot name/password, default bridge URL, token |
| `bridge/config.json` | Port, token, public URL, OpenCode path, model, rate limit, timeout |
| `bridge/start-hotspot.ps1` | Hotspot SSID, password, 2.4 GHz band |
| `bridge/setup-tunnel.ps1` | Tunnel name, public hostname, bridge port |

The token must match in `secrets.h` and `config.json`. It is the only thing
protecting the public endpoint, so keep it long and change it if it leaks.

The Wi-Fi network, bridge URL, and token can all be changed from the setup
hotspot without reflashing. The values in `secrets.h` are only defaults for a
board that has never been configured.

To use a different model, change `model` in `bridge/config.json` to any model
shown by `opencode models`, for example:

```json
"model": { "providerID": "ollama", "id": "llama3.2:3b" }
```

`fallbackModels` is the ordered backup list. The bridge tries each entry in
turn. A model that returns a rate-limit or quota error is skipped for its
cooldown period, so the next message goes straight to the fallback.

For a local-only setup, swap the two entries:

```json
"model": { "providerID": "ollama", "id": "qwen2.5:7b" },
"fallbackModels": []
```

For a cloud-only setup, use the free model with an empty fallback list. The bot
will return an error while the free quota is exhausted. OpenCode Go
(<https://opencode.ai/go>) is the paid option for reliable cloud access.

Restart the bridge after changing the config. `GET /health` shows the current
model chain, the last model used, and any active cooldowns.

## How it works

1. The board joins whatever Wi-Fi is available and starts an HTTP server.
2. Every five minutes it POSTs its LAN address to the bridge over HTTPS.
3. Your phone opens <https://chat.blueflutex.in/>; the bridge looks up the
   board's last known address and redirects the phone to it.
4. The browser loads the chat page from the board, over the local Wi-Fi.
5. Each message is POSTed to `/api/chat` on the board with the recent history.
6. The board forwards the JSON to the bridge through the Cloudflare tunnel.
7. The bridge builds a prompt and calls OpenCode's `generate/text` endpoint.
8. OpenCode runs the model and the reply travels back the same way.

The page keeps the last 40 messages in `sessionStorage`; the bridge is
stateless. The board stores only its Wi-Fi and bridge settings.

Why the redirect dance: the board has no fixed public address, and giving it
one would mean inbound connections to a device that sits on random Wi-Fi.
Outbound HTTPS from the board is the only thing that works everywhere.

## Troubleshooting

**The page says "no bridge" (LED slow blinks)**
- The board is on Wi-Fi but cannot reach the PC. Check, in order:
  - the PC is on and the bridge window is open
  - the tunnel window is open, or run `bridge\start-tunnel.cmd`
  - `Invoke-RestMethod https://chat.blueflutex.in/health` answers
  - `Invoke-RestMethod http://127.0.0.1:8787/health` answers on the PC
- The board retries every five minutes, or press reset to retry now.

**The page says "offline"**
- The browser cannot reach the board, so the phone is on a different network.
  Join the same Wi-Fi as the board and reload.
- Check the ESP32 serial monitor for its IP and Wi-Fi status.

**chat.blueflutex.in says "the board is not reachable"**
- The bridge has not heard from the board in 20 minutes. The board is off, or
  it has not joined this Wi-Fi yet.
- `Invoke-RestMethod https://chat.blueflutex.in/where` shows the last address
  the bridge knows and how old it is.

**The page loads but the chat area is empty and the status stays "connecting"**
- That means a JavaScript error stopped the page script. Check what the board
  actually serves:

```powershell
$html = (Invoke-WebRequest http://192.168.137.135/ -UseBasicParsing).Content
$html -match '#line|function \w+\(\);'
```

  A match means the Arduino preprocessor mangled the embedded page. Keep the
  HTML in `web_page.h` and rebuild with `flash.cmd`.

**"Bridge unreachable" in the chat**
- The board talks to the bridge over HTTPS through the tunnel, so the PC
  firewall is not involved. Check the tunnel first.
- If you deliberately switched the board to the LAN address
  (`http://192.168.137.1:8787` in the setup hotspot), then the firewall matters:

```powershell
New-NetFirewallRule -DisplayName "ESP32 OpenCode Bridge" -Direction Inbound `
  -Action Allow -Protocol TCP -LocalPort 8787 -RemoteAddress 192.168.137.0/24
```

**Someone is using my bridge / I see unexpected chat logs**
- The public endpoint is protected by the token in `config.json` and a rate
  limit of 40 messages per 5 minutes per IP. Change the token in
  `bridge/config.json` and in the board's setup hotspot to lock it again.
- Cloudflare Access can add a login in front of the tunnel, but the ESP32
  cannot complete an interactive login, so it is not used here.

**"Free usage limit reached" or a provider error**
- The bridge uses the free cloud model first and falls back to Ollama. A
  "Free usage limit reached" line in the bridge log is expected when the cloud
  quota is exhausted; the reply still arrives from Ollama.
- If the chat fails instead, check that Ollama is running (`ollama list`) and
  that the model IDs in `bridge/config.json` match `opencode models`.
- For reliable cloud access, subscribe to OpenCode Go and set its model in
  `bridge/config.json`.

**The first reply is slow**
- The model is loading into VRAM. The bridge warms it up at startup; wait a
  few seconds and retry.
- Ollama unloads an idle model after about 5 minutes, so the first message
  after a long pause can also be slow. Later replies are fast again.

**The ESP32 does not appear on COM5**
- Replug it and run `arduino-cli board list`. Use the port it reports.
- The CP210x driver is required; install it from Windows Update if needed.

**The hotspot will not start**
- Windows Settings -> Network & internet -> Mobile hotspot -> turn on.
- Set the band to 2.4 GHz and the name to `ESP32-Chat`.

## Notes

- MCP is not a model server. It is a tool protocol. OpenCode can use MCP
  servers later to give the bot tools (weather, home automation, search), but
  the language model still runs on the PC or in the cloud.
- The ESP32 has no microphone or speaker. Voice is a future add-on: an I2S mic
  and amplifier can be wired to the board, with speech-to-text and
  text-to-speech running on the PC.
- The bridge binds to `0.0.0.0:8787`, so both the tunnel and LAN clients can
  reach it. The LAN firewall rule stays scoped to `192.168.137.0/24`.
- TLS on the board uses `setInsecure()`. Cloudflare rotates its edge
  certificates between CAs, so pinning a root would eventually brick the
  device. The token is the real access control; TLS protects the payload in
  transit, not the endpoint identity.
- Cloudflare proxies requests with a 100-second origin timeout. A model call
  that runs longer returns a 524 error even though the bridge keeps working.
  Keep `timeoutMs` under that.

---

Built by **BlueFluteX**

- Website: https://blueflutex.in/
- Location: Pune, Maharashtra, India
- Contact: support@blueflutex.in
